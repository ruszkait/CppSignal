#include "CppSignal.h"

using namespace CppSignal;

Subscription::Subscription()
{}

Subscription::Subscription(std::weak_ptr<IRegistration>&& registration)
	: _registration(std::move(registration))
{}

Subscription::Subscription(Subscription && other)
	: _registration(std::move(other._registration))
{}

Subscription& Subscription::operator=(Subscription&& other)
{
	bool isSelfAssignment = this == &other;
	if (isSelfAssignment)
		return *this;

	Unsubscribe();

	_registration = std::move(other._registration);
	return *this;
}

void CppSignal::Subscription::Unsubscribe()
{
	auto publisherStrongReference = _registration.lock();
	_registration.reset();

	auto publisherStrongReferenceIsInvalid = !publisherStrongReference;
	if (publisherStrongReferenceIsInvalid)
		return;

	auto& registrationReference = publisherStrongReference;
	registrationReference->Deallocate();
}

Subscription::~Subscription()
{
	Unsubscribe();
}
