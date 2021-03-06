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
	class IRegistration
	{
	public:
		virtual ~IRegistration() = default;
		virtual void Deallocate() = 0;
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
		class Registration : public IRegistration
		{
		public:
			bool TryAllocate(const Callback& callback);
			bool TryAllocate(Callback&& callback);

			void Deallocate() override;

			void Emit(TSignalParameters ... signalParameters);

		private:
			enum class RegistrationStatus
			{
				Unknown,
				Empty,
				Populating,
				Used,
				Emitting,
				Destroying,
			};

			std::atomic<RegistrationStatus> _status = RegistrationStatus::Empty;
			Callback _callback;
		};

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

		Subscription(std::weak_ptr<IRegistration>&& registration);

		// Aliasing smart pointer: the control block belongs to the publisher, the pointer belongs to the registration
		std::weak_ptr<IRegistration> _registration;
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
		for (auto& registration : _registrations)
		{
			auto subscrptionAccepted = registration.TryAllocate(callback);
			if (subscrptionAccepted)
			{
				// Aliasing smart pointer: holds the control blocks of the publisher and the pointer to the registration
				auto registrationPointer = std::shared_ptr<IRegistration>(publisher, &registration);
				return Subscription(registrationPointer);
			}
		}

		// No free registration left
		throw std::bad_alloc();
	}

	template<typename ...TSignalParameters>
	template<typename TPublisher>
	Subscription Signal<TSignalParameters...>::Subscribe(const std::shared_ptr<TPublisher>& publisher, Callback && callback)
	{
		for (auto& registration : _registrations)
		{
			auto subscrptionAccepted = registration.TryAllocate(std::move(callback));
			if (subscrptionAccepted)
			{
				auto registrationPointer = std::shared_ptr<IRegistration>(publisher, &registration);
				return Subscription(registrationPointer);
			}
		}

		// No free registration left
		throw std::bad_alloc();
	}

	template<typename ...TSignalParameters>
	void Signal<TSignalParameters...>::Emit(TSignalParameters ... signalParameters)
	{
		for (auto& registration : _registrations)
			registration.Emit(std::forward<TSignalParameters ...>(signalParameters)...);
	}

// ==========================================================================================================================

	template<typename ...TSignalParameters>
	bool Signal<TSignalParameters...>::Registration::TryAllocate(const Callback & callback)
	{
		auto lastKnownCurrentRegistrationStatus = RegistrationStatus::Empty;
		auto transitionSucceeded = _status.compare_exchange_strong(lastKnownCurrentRegistrationStatus, RegistrationStatus::Populating);
		if (!transitionSucceeded)
			return false;

		_callback = callback;

		lastKnownCurrentRegistrationStatus = RegistrationStatus::Populating;
		transitionSucceeded = _status.compare_exchange_strong(lastKnownCurrentRegistrationStatus, RegistrationStatus::Used);
		assert(transitionSucceeded);
		
		return true;
	}

	template<typename ...TSignalParameters>
	bool Signal<TSignalParameters...>::Registration::TryAllocate(Callback && callback)
	{
		auto lastKnownCurrentRegistrationStatus = RegistrationStatus::Empty;
		auto transitionSucceeded = _status.compare_exchange_strong(lastKnownCurrentRegistrationStatus, RegistrationStatus::Populating);
		if (!transitionSucceeded)
			return false;

		_callback = std::move(callback);

		lastKnownCurrentRegistrationStatus = RegistrationStatus::Populating;
		transitionSucceeded = _status.compare_exchange_strong(lastKnownCurrentRegistrationStatus, RegistrationStatus::Used);
		assert(transitionSucceeded);

		return true;
	}

	template<typename ...TSignalParameters>
	void Signal<TSignalParameters...>::Registration::Deallocate()
	{
		while (true)
		{
			RegistrationStatus lastKnownRegistrationStatus = _status;
			switch (lastKnownRegistrationStatus)
			{
				case RegistrationStatus::Used:
				{
					// If multiple threads come here, only one of them manages to do the transition
					// The one, that made the transition is responsible to cleanup the Registration
					auto transitionSucceeded = _status.compare_exchange_strong(lastKnownRegistrationStatus, RegistrationStatus::Destroying);
					if (transitionSucceeded)
					{
						try
						{
							// The std::function destructor might throw exceptions
							_callback = nullptr;
						}
						catch (...)
						{ }

						lastKnownRegistrationStatus = RegistrationStatus::Destroying;
						transitionSucceeded = _status.compare_exchange_strong(lastKnownRegistrationStatus, RegistrationStatus::Empty);
						assert(transitionSucceeded);
						return;
					}
				}
				break;
				case RegistrationStatus::Emitting:
				{
					auto transitionSucceeded = _status.compare_exchange_strong(lastKnownRegistrationStatus, RegistrationStatus::Destroying);
					if (transitionSucceeded)
						return;
				}
				break;
				case RegistrationStatus::Empty:
				case RegistrationStatus::Destroying:
					// We end up here if multiple threads try to free this Registration
					// The job was done anyway, so we can return from here
					return;
				default:
					assert(false);
			}
		}
	}
	
	template<typename ...TSignalParameters>
	void Signal<TSignalParameters...>::Registration::Emit(TSignalParameters ... signalParameters)
	{
		RegistrationStatus lastExpectedCurrentRegistrationStatus;

		bool transitionSucceeded;
		while (true)
		{
			lastExpectedCurrentRegistrationStatus = RegistrationStatus::Used;
			transitionSucceeded = _status.compare_exchange_strong(lastExpectedCurrentRegistrationStatus, RegistrationStatus::Emitting);
			if (transitionSucceeded)
				break;

			auto anEmissionIsAlreadyRunningFromAnotherThread = lastExpectedCurrentRegistrationStatus == RegistrationStatus::Emitting;
			if (anEmissionIsAlreadyRunningFromAnotherThread)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				continue;
			}

			// The Registration is not in a state where emission is possible
			return;
		}

		try
		{
			// The callback exceptions must be handled here, otherwise they might mess up the state administration
			_callback(std::forward<TSignalParameters ...>(signalParameters)...);
		}
		catch(...)
		{ }

		lastExpectedCurrentRegistrationStatus = RegistrationStatus::Emitting;
		transitionSucceeded = _status.compare_exchange_strong(lastExpectedCurrentRegistrationStatus, RegistrationStatus::Used);

		auto registrationWasDestroyedDuringEmission = !transitionSucceeded;
		if (registrationWasDestroyedDuringEmission)
		{
			lastExpectedCurrentRegistrationStatus = _status;
			assert(lastExpectedCurrentRegistrationStatus == RegistrationStatus::Destroying);

			try
			{
				// The std::function destructor might throw exceptions
				_callback = nullptr;
			}
			catch(...)
			{ }

			transitionSucceeded = _status.compare_exchange_strong(lastExpectedCurrentRegistrationStatus, RegistrationStatus::Empty);
			assert(transitionSucceeded);
		}
	}
}
