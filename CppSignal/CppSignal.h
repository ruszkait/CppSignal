#pragma once

#include <functional>
#include <atomic>
#include <memory>
#include <vector>
#include <utility>
#include <thread>
#include <chrono>

namespace CppSignal
{
	enum class RegistrationStatus
	{
		Unknown,
		Empty,
		Populating,
		Used,
		Emitting,
		Destroying,
		PendingDestruction
	};

	class IRegistation
	{
	public:
		IRegistation();

		virtual ~IRegistation() = default;
		virtual void Unsubscribe() = 0;

		std::atomic<RegistrationStatus> _status;
	};

	class Subscription;

	template<typename ...TSignalParameters>
	class Signal
	{
	public:
		using Callback = std::function<void(TSignalParameters...)>;

		Signal(int numberOfMaxRegistrations = 5);

		template<typename TPublisher>
		Subscription Subscribe(const std::shared_ptr<TPublisher>& publisher, const Callback& callback);

		template<typename TPublisher>
		Subscription Subscribe(const std::shared_ptr<TPublisher>& publisher, Callback&& callback);
		
		void Emit(TSignalParameters... signalParameters);

	private:
		class Registration : public IRegistation
		{
		public:
			void Unsubscribe() override;

			void Emit(TSignalParameters ... signalParameters);

			Callback _callback;
		};

		IRegistation & SaveCallbackToAnUnusedRegistration(const Callback& callback);
		IRegistation & SaveCallbackToAnUnusedRegistration(Callback&& callback);

		// This container must not reallocate, because the subscriptions directly hold an aliasing smart pointer to the registration instances
		std::vector<Registration> _registrations;
	};

	class Subscription
	{
	public:
		Subscription();

		Subscription(Subscription&& other);

		Subscription& operator=(Subscription&& other);

		void Unsubscribe();

		~Subscription();

	private:
		template<typename ...TSignalParameters>
		friend class Signal;

		Subscription(std::weak_ptr<IRegistation>&& registration);

		// Aliasing smart pointer: the control block belongs to the publisher, the pointer belongs to the registration
		std::weak_ptr<IRegistation> _registration;
	};

// ==========================================================================================================================

	template<typename ...TSignalParameters>
	Signal<TSignalParameters...>::Signal(int numberOfMaxRegistrations)
		: _registrations(numberOfMaxRegistrations)
	{}

	template<typename ...TSignalParameters>
	template<typename TPublisher>
	Subscription Signal<TSignalParameters...>::Subscribe(const std::shared_ptr<TPublisher>& publisher, const Callback& callback)
	{
		// This template method is duplicated for each Publisher type. To minimize the code here, the saving of the callback is outsourced
		auto& registration = SaveCallbackToAnUnusedRegistration(callback);

		auto registrationPointer = std::shared_ptr<IRegistation>(publisher, &registration);
		return Subscription(registrationPointer);
	}

	template<typename ...TSignalParameters>
	template<typename TPublisher>
	Subscription Signal<TSignalParameters...>::Subscribe(const std::shared_ptr<TPublisher>& publisher, Callback && callback)
	{
		auto& registration = SaveCallbackToAnUnusedRegistration(std::move(callback));

		auto registrationPointer = std::shared_ptr<IRegistation>(publisher, &registration);
		return Subscription(registrationPointer);
	}

	template<typename ...TSignalParameters>
	void Signal<TSignalParameters...>::Emit(TSignalParameters ... signalParameters)
	{
		for (auto& registration : _registrations)
		{
			registration.Emit(std::forward<TSignalParameters ...>(signalParameters)...);
		}
	}

	template<typename ...TSignalParameters>
	IRegistation & Signal<TSignalParameters...>::SaveCallbackToAnUnusedRegistration(const Callback & callback)
	{
		for (auto& registration : _registrations)
		{
			auto lastKnownCurrentRegistrationStatus = RegistrationStatus::Empty;
			auto transitionSucceeded = registration._status.compare_exchange_strong(lastKnownCurrentRegistrationStatus, RegistrationStatus::Populating);
			if (!transitionSucceeded)
				continue;

			registration._callback = callback;

			lastKnownCurrentRegistrationStatus = RegistrationStatus::Populating;
			transitionSucceeded = registration._status.compare_exchange_strong(lastKnownCurrentRegistrationStatus, RegistrationStatus::Used);
			assert(transitionSucceeded);

			return registration;
		}

		// No free registration left
		throw std::bad_alloc();
	}

	template<typename ...TSignalParameters>
	IRegistation & Signal<TSignalParameters...>::SaveCallbackToAnUnusedRegistration(Callback && callback)
	{
		for (auto& registration : _registrations)
		{
			auto lastKnownCurrentRegistrationStatus = RegistrationStatus::Empty;
			auto transitionSucceeded = registration._status.compare_exchange_strong(lastKnownCurrentRegistrationStatus, RegistrationStatus::Populating);
			if (!transitionSucceeded)
				continue;

			registration._callback = std::move(callback);

			lastKnownCurrentRegistrationStatus = RegistrationStatus::Populating;
			transitionSucceeded = registration._status.compare_exchange_strong(lastKnownCurrentRegistrationStatus, RegistrationStatus::Used);
			assert(transitionSucceeded);

			return registration;
		}

		// No free registration left
		throw std::bad_alloc();
	}

	// ==========================================================================================================================

	template<typename ...TSignalParameters>
	void Signal<TSignalParameters...>::Registration::Unsubscribe()
	{
		bool stayInLoop = true;
		while (stayInLoop)
		{
			RegistrationStatus lastKnownRegistrationStatus = _status;
			switch (lastKnownRegistrationStatus)
			{
				case RegistrationStatus::Used:
				{
					auto transitionSucceeded = _status.compare_exchange_strong(lastKnownRegistrationStatus, RegistrationStatus::Destroying);
					if (transitionSucceeded)
					{
						_callback = nullptr;
						_status = RegistrationStatus::Empty;
						stayInLoop = false;
					}
					break;
				}
				case RegistrationStatus::Emitting:
				{
					auto transitionSucceeded = _status.compare_exchange_strong(lastKnownRegistrationStatus, RegistrationStatus::PendingDestruction);
					if (transitionSucceeded)
						stayInLoop = false;
					break;
				}
			}
		}
	}
	
	template<typename ...TSignalParameters>
	void Signal<TSignalParameters...>::Registration::Emit(TSignalParameters ... signalParameters)
	{
		RegistrationStatus lastKnownCurrentRegistrationStatus;

		bool transitionSucceeded;
		while (true)
		{
			lastKnownCurrentRegistrationStatus = RegistrationStatus::Used;
			transitionSucceeded = _status.compare_exchange_strong(lastKnownCurrentRegistrationStatus, RegistrationStatus::Emitting);
			if (transitionSucceeded)
				break;

			auto anEmissionIsAlreadyRunningFromAnotherThread = lastKnownCurrentRegistrationStatus == RegistrationStatus::Emitting;
			if (anEmissionIsAlreadyRunningFromAnotherThread)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				continue;
			}

			return;
		}

		_callback(std::forward<TSignalParameters ...>(signalParameters)...);

		lastKnownCurrentRegistrationStatus = RegistrationStatus::Emitting;
		transitionSucceeded = _status.compare_exchange_strong(lastKnownCurrentRegistrationStatus, RegistrationStatus::Used);

		auto registrationWasDestroyedDuringEmission = !transitionSucceeded;
		if (registrationWasDestroyedDuringEmission)
		{
			lastKnownCurrentRegistrationStatus = _status;
			assert(lastKnownCurrentRegistrationStatus == RegistrationStatus::PendingDestruction);

			_callback = nullptr;

			transitionSucceeded = _status.compare_exchange_strong(lastKnownCurrentRegistrationStatus, RegistrationStatus::Empty);
			assert(transitionSucceeded);
		}
	}

}
