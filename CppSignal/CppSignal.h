#pragma once

#include <functional>
#include <atomic>
#include <memory>
#include <vector>
#include <utility>

namespace CppSignal
{
	enum class SlotStatus
	{
		Unknown,
		Empty,
		Populating,
		Used,
		Emitting,
		Destroying,
		PendingDestruction
	};

	class ISlot
	{
	public:
		ISlot();

		virtual ~ISlot() = default;
		virtual void Unsubscribe() = 0;

		std::atomic<SlotStatus> _status;
	};

	class Subscription;

	template<typename ...TSignalParameters>
	class Signal
	{
	public:
		using Callback = std::function<void(TSignalParameters...)>;

		class Slot : public ISlot
		{
		public:
			void Unsubscribe() override;

			Callback _callback;
		};

		Signal(int numberOfMaxSlots = 5);

		template<typename TPublisher>
		Subscription Subscribe(const std::shared_ptr<TPublisher>& publisher, const Callback& callback);

		template<typename TPublisher>
		Subscription Subscribe(const std::shared_ptr<TPublisher>& publisher, Callback&& callback);
		
		void Emit(TSignalParameters... signalParameters);

	private:
		ISlot & SaveCallbackToAnUnusedSlot(const Callback& callback);
		ISlot & SaveCallbackToAnUnusedSlot(Callback&& callback);

		// This container must not reallocate, because the subscriptions directly hold an aliasing smart pointer to the slot instances
		std::vector<Slot> _slots;
	};

	class Subscription
	{
	public:
		Subscription();

		Subscription(const std::weak_ptr<ISlot>& slot);

		Subscription(Subscription&& other);

		Subscription& operator=(Subscription&& other);

		~Subscription();

	private:
		// Aliasing smart pointer: the control block belongs to the publisher, the pointer belongs to the slot
		std::weak_ptr<ISlot> _slot;
	};

// ==========================================================================================================================

	template<typename ...TSignalParameters>
	template<typename TPublisher>
	Subscription Signal<TSignalParameters...>::Subscribe(const std::shared_ptr<TPublisher>& publisher, const Callback& callback)
	{
		// This template method is duplicated for each Publisher type. To minimize the code here, the saving of the callback is outsourced
		auto& slot = SaveCallbackToAnUnusedSlot(callback);

		auto slotPointer = std::shared_ptr<ISlot>(publisher, &slot);
		return Subscription(std::weak_ptr<ISlot>(slotPointer));
	}

	template<typename ...TSignalParameters>
	template<typename TPublisher>
	Subscription Signal<TSignalParameters...>::Subscribe(const std::shared_ptr<TPublisher>& publisher, Callback && callback)
	{
		auto& slot = SaveCallbackToAnUnusedSlot(std::move(callback));

		auto slotPointer = std::shared_ptr<ISlot>(publisher, &slot);
		return Subscription(std::weak_ptr<ISlot>(slotPointer));
	}

	template<typename ...TSignalParameters>
	Signal<TSignalParameters...>::Signal(int numberOfMaxSlots)
		: _slots(numberOfMaxSlots)
	{}

	template<typename ...TSignalParameters>
	void Signal<TSignalParameters...>::Emit(TSignalParameters ... signalParameters)
	{
		for (auto& slot : _slots)
		{
			SlotStatus lastKnownCurrentSlotStatus = slot._status;
			if (lastKnownCurrentSlotStatus != SlotStatus::Used)
				continue;

			auto transitionSucceeded = slot._status.compare_exchange_strong(lastKnownCurrentSlotStatus, SlotStatus::Emitting);
			if (!transitionSucceeded)
				continue;

			slot._callback(std::forward<TSignalParameters ...>(signalParameters)...);

			lastKnownCurrentSlotStatus = SlotStatus::Emitting;
			transitionSucceeded = slot._status.compare_exchange_strong(lastKnownCurrentSlotStatus, SlotStatus::Used);

			auto slotWasDestroyedDuringEmission = !transitionSucceeded;
			if (slotWasDestroyedDuringEmission)
			{
				lastKnownCurrentSlotStatus = slot._status;
				assert(lastKnownCurrentSlotStatus == SlotStatus::PendingDestruction);

				slot._callback = nullptr;

				transitionSucceeded = slot._status.compare_exchange_strong(lastKnownCurrentSlotStatus, SlotStatus::Empty);
				assert(transitionSucceeded);
			}
		}
	}

	template<typename ...TSignalParameters>
	ISlot & Signal<TSignalParameters...>::SaveCallbackToAnUnusedSlot(const Callback & callback)
	{
		for (auto& slot : _slots)
		{
			SlotStatus lastKnownCurrentSlotStatus = slot._status;
			if (lastKnownCurrentSlotStatus != SlotStatus::Empty)
				continue;

			auto transitionSucceeded = slot._status.compare_exchange_strong(lastKnownCurrentSlotStatus, SlotStatus::Populating);
			if (!transitionSucceeded)
				continue;

			slot._callback = callback;

			lastKnownCurrentSlotStatus = SlotStatus::Populating;
			transitionSucceeded = slot._status.compare_exchange_strong(lastKnownCurrentSlotStatus, SlotStatus::Used);
			assert(transitionSucceeded);

			return slot;
		}

		// No free slot left
		throw std::bad_alloc();
	}

	template<typename ...TSignalParameters>
	inline ISlot & Signal<TSignalParameters...>::SaveCallbackToAnUnusedSlot(Callback && callback)
	{
		for (auto& slot : _slots)
		{
			SlotStatus lastKnownCurrentSlotStatus = slot._status;
			if (lastKnownCurrentSlotStatus != SlotStatus::Empty)
				continue;

			auto transitionSucceeded = slot._status.compare_exchange_strong(lastKnownCurrentSlotStatus, SlotStatus::Populating);
			if (!transitionSucceeded)
				continue;

			slot._callback = std::move(callback);

			lastKnownCurrentSlotStatus = SlotStatus::Populating;
			transitionSucceeded = slot._status.compare_exchange_strong(lastKnownCurrentSlotStatus, SlotStatus::Used);
			assert(transitionSucceeded);

			return slot;
		}

		// No free slot left
		throw std::bad_alloc();
	}

	template<typename ...TSignalParameters>
	void Signal<TSignalParameters...>::Slot::Unsubscribe()
	{
		bool stayInLoop = true;
		while (stayInLoop)
		{
			SlotStatus lastKnownSlotStatus = _status;
			switch (lastKnownSlotStatus)
			{
				case SlotStatus::Used:
				{
					auto transitionSucceeded = _status.compare_exchange_strong(lastKnownSlotStatus, SlotStatus::Destroying);
					if (transitionSucceeded)
					{
						_callback = nullptr;
						_status = SlotStatus::Empty;
						stayInLoop = false;
					}
					break;
				}
				case SlotStatus::Emitting:
				{
					auto transitionSucceeded = _status.compare_exchange_strong(lastKnownSlotStatus, SlotStatus::PendingDestruction);
					if (transitionSucceeded)
						stayInLoop = false;
					break;
				}
			}
		}
	}
}
