#pragma once

#include <functional>
#include <atomic>
#include <memory>
#include <array>

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

		template<typename TPublisher>
		Subscription Subscribe(const std::shared_ptr<TPublisher>& publisher, const Callback& callback);

		void Emit(TSignalParameters... signalParameters);

	private:
		std::array<Slot, 10> _slots;
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
		for (auto& slot : _slots)
		{
			SlotStatus lastKnownCurrentSlotStatus = slot._status;
			if (lastKnownCurrentSlotStatus != SlotStatus::Empty)
				continue;

			auto transitionSucceeded = slot._status.compare_exchange_strong(lastKnownCurrentSlotStatus, SlotStatus::Populating);
			if (!transitionSucceeded)
				continue;

			slot._callback = callback;
			slot._status = SlotStatus::Used;

			auto slotPointer = std::shared_ptr<ISlot>(publisher, &slot);
			return Subscription(std::weak_ptr<ISlot>(slotPointer));
		}

		// No free slot left
		throw std::bad_alloc();
	}

	template<typename ...TSignalParameters>
	void Signal<TSignalParameters...>::Emit(TSignalParameters ... signalParameters)
	{
		for (auto& slot : _slots)
		{
			SlotStatus lastKnownCurrentSlotStatus = slot._status;
			auto slotIsNotEligibleForEmission = lastKnownCurrentSlotStatus != SlotStatus::Used;
			if (slotIsNotEligibleForEmission)
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
