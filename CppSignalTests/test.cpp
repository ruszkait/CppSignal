#include "pch.h"
#include "../CppSignal/CppSignal.h"

class Publisher : public std::enable_shared_from_this<Publisher>
{
public:
	CppSignal::Subscription OnTemperatureChanged(const std::function<void(double)> callback)
	{
		// The signal publisher shall be instantiated via a smart pointer
		// This way the subsctiption can avoid unsubscribing from a dead publisher
		auto publisher = shared_from_this();
		auto subscription = _signalTemperatureChanged.Subscribe(publisher, callback);
		return subscription;
	}

	CppSignal::Subscription OnFreezing(const std::function<void()> callback)
	{
		return _signalFreezing.Subscribe(shared_from_this(), callback);
	}

	void UpdateTemperature(double newTmeprature)
	{
		_signalTemperatureChanged.Emit(newTmeprature);
		if (newTmeprature < 0.0)
			_signalFreezing.Emit();
	}

private:
	CppSignal::Signal<double> _signalTemperatureChanged;
	CppSignal::Signal<> _signalFreezing;
};

TEST(ScopedSubscriptionTest, CppSignalTest)
{
	auto temperatureNotification = 0.0;
	auto freezingDays = 0;

	auto publisher = std::make_shared<Publisher>();
	{
		auto sub1 = publisher->OnTemperatureChanged([&temperatureNotification](double value) { temperatureNotification = value; });
		auto sub2 = publisher->OnFreezing([&freezingDays]() { freezingDays++; });

		publisher->UpdateTemperature(40);
		EXPECT_DOUBLE_EQ(40.0, temperatureNotification);
		EXPECT_EQ(0, freezingDays);

		publisher->UpdateTemperature(-10);
		EXPECT_DOUBLE_EQ(-10.0, temperatureNotification);
		EXPECT_EQ(1, freezingDays);
	}

	// Subscriptions are gone, so the signals do not call the callbacks
	publisher->UpdateTemperature(20);
	EXPECT_DOUBLE_EQ(-10.0, temperatureNotification);
	EXPECT_EQ(1, freezingDays);
}

TEST(MovedSubscriptionTest, CppSignalTest)
{
	auto temperatureNotification = 0.0;
	auto freezingDays = 0;

	CppSignal::Subscription subScription;

	auto publisher = std::make_shared<Publisher>();
	{
		auto sub1 = publisher->OnTemperatureChanged([&temperatureNotification](double value) { temperatureNotification = value; });
		auto sub2 = publisher->OnFreezing([&freezingDays]() { freezingDays++; });

		// Move the first subscription to the outer scopem but not the second one
		subScription = std::move(sub1);

		publisher->UpdateTemperature(40);
		EXPECT_DOUBLE_EQ(40.0, temperatureNotification);
		EXPECT_EQ(0, freezingDays);

		publisher->UpdateTemperature(-10);
		EXPECT_DOUBLE_EQ(-10.0, temperatureNotification);
		EXPECT_EQ(1, freezingDays);
	}

	// Subscriptions are gone, so the signals do not call the callbacks
	publisher->UpdateTemperature(-20);
	EXPECT_DOUBLE_EQ(-20.0, temperatureNotification);
	EXPECT_EQ(1, freezingDays);
}

TEST(AbandonedPublisherTest, CppSignalTest)
{
	auto temperatureNotification = 0.0;
	auto freezingDays = 0;

	auto publisher = std::make_shared<Publisher>();
	{
		auto sub1 = publisher->OnTemperatureChanged([&temperatureNotification](double value) { temperatureNotification = value; });
		auto sub2 = publisher->OnFreezing([&freezingDays]() { freezingDays++; });

		// Remove the publisher right after the subscriptions
		publisher.reset();

		// The unsubscription shall detect the lack of the publisher, and shall still destroy silently
	}
}

TEST(SharedSubscriptionTest, CppSignalTest)
{
	auto temperatureNotification = 0.0;
	auto sharedSubscription = std::make_shared<CppSignal::Subscription>();

	auto publisher = std::make_shared<Publisher>();
	{
		// Move the subscription to the shared subscription
		*sharedSubscription = publisher->OnTemperatureChanged([&temperatureNotification](double value) { temperatureNotification = value; });

		auto sharedSubscription2 = sharedSubscription;
		auto sharedSubscription1 = sharedSubscription;

		// From here on the two other smart pointer keep the subscription alive
		sharedSubscription.reset();

		publisher->UpdateTemperature(40);
		EXPECT_DOUBLE_EQ(40.0, temperatureNotification);

		publisher->UpdateTemperature(-10);
		EXPECT_DOUBLE_EQ(-10.0, temperatureNotification);
	}

	// Subscriptions are gone, so the signals do not call the callbacks
	publisher->UpdateTemperature(-20);
	EXPECT_DOUBLE_EQ(-10.0, temperatureNotification);
}
