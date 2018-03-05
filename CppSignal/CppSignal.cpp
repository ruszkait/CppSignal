#include "CppSignal.h"

using namespace CppSignal;

ISlot::ISlot()
	: _status(SlotStatus::Empty)
{
}

Subscription::Subscription()
{}

Subscription::Subscription(const std::weak_ptr<ISlot>& slot)
	: _slot(slot)
{}

Subscription::Subscription(Subscription && other)
	: _slot(std::move(other._slot))
{}

Subscription& Subscription::operator=(Subscription&& other)
{
	if (this == &other)
		return *this;

	_slot = std::move(other._slot);
	return *this;
}

Subscription::~Subscription()
{
	auto publisherStrongReference = _slot.lock();

	auto publisherStrongReferenceIsInvalid = !publisherStrongReference;
	if (publisherStrongReferenceIsInvalid)
		return;

	auto& slotReference = publisherStrongReference;
	slotReference->Unsubscribe();
}
