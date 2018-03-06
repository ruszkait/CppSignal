#include "pch.h"
#include "../CppSignal/CppSignal.h"

class Thermometer : public std::enable_shared_from_this<Thermometer>
{
public:

	// This template subscription covers the const reference passing and the lvalue moving
	template<typename TCallback>
	CppSignal::Subscription OnTemperatureChanged(TCallback&& callback)
	{
		// The signal publisher shall be instantiated via a smart pointer
		// This way the subscription can avoid unsubscribing from a dead publisher
		auto publisher = shared_from_this();
		// The TCallback is forwarded till here and it gets converted into and std::function by the Subscribe call implicit argument conversion
		// This is OK, anyway, because this method is small and most likely gets inlined at the place of invoking
		auto subscription = _signalTemperatureChanged.Subscribe(publisher, std::forward<TCallback&&>(callback));
		return subscription;
	}

	template<typename TCallback>
	CppSignal::Subscription OnFreezing(TCallback&& callback)
	{
		return _signalFreezing.Subscribe(shared_from_this(), std::forward<TCallback&&>(callback));
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

	auto thermometer = std::make_shared<Thermometer>();
	{
		// Subscribe with an rvalue lambda - most efficient, because of the moving semantic
		auto sub1 = thermometer->OnTemperatureChanged([&temperatureNotification](double value) { temperatureNotification = value; });
		
		// Subscribe with an lvalue function - the function gets copied to signal registrations
		std::function<void()> freezingCallback = [&freezingDays]() { freezingDays++; };
		auto sub2 = thermometer->OnFreezing(freezingCallback);

		thermometer->UpdateTemperature(40);
		EXPECT_DOUBLE_EQ(40.0, temperatureNotification);
		EXPECT_EQ(0, freezingDays);

		thermometer->UpdateTemperature(-10);
		EXPECT_DOUBLE_EQ(-10.0, temperatureNotification);
		EXPECT_EQ(1, freezingDays);
	}

	// Subscriptions are gone, so the signals do not call the callbacks
	thermometer->UpdateTemperature(20);
	EXPECT_DOUBLE_EQ(-10.0, temperatureNotification);
	EXPECT_EQ(1, freezingDays);
}

TEST(MovedSubscriptionTest, CppSignalTest)
{
	auto temperatureNotification = 0.0;
	auto freezingDays = 0;

	CppSignal::Subscription subScription;

	auto thermometer = std::make_shared<Thermometer>();
	{
		auto sub1 = thermometer->OnTemperatureChanged([&temperatureNotification](double value) { temperatureNotification = value; });
		auto sub2 = thermometer->OnFreezing([&freezingDays]() { freezingDays++; });

		// Move the first subscription to the outer scopem but not the second one
		subScription = std::move(sub1);

		thermometer->UpdateTemperature(40);
		EXPECT_DOUBLE_EQ(40.0, temperatureNotification);
		EXPECT_EQ(0, freezingDays);

		thermometer->UpdateTemperature(-10);
		EXPECT_DOUBLE_EQ(-10.0, temperatureNotification);
		EXPECT_EQ(1, freezingDays);
	}

	// Subscriptions are gone, so the signals do not call the callbacks
	thermometer->UpdateTemperature(-20);
	EXPECT_DOUBLE_EQ(-20.0, temperatureNotification);
	EXPECT_EQ(1, freezingDays);
}

TEST(AbandonedPublisherTest, CppSignalTest)
{
	auto temperatureNotification = 0.0;
	auto freezingDays = 0;

	auto thermometer = std::make_shared<Thermometer>();
	{
		auto sub1 = thermometer->OnTemperatureChanged([&temperatureNotification](double value) { temperatureNotification = value; });
		auto sub2 = thermometer->OnFreezing([&freezingDays]() { freezingDays++; });

		// Remove the publisher right after the subscriptions
		thermometer.reset();

		// The unsubscription shall detect the lack of the publisher, and shall still destroy silently
	}
}

TEST(SharedSubscriptionTest, CppSignalTest)
{
	auto temperatureNotification = 0.0;
	auto sharedSubscription = std::make_shared<CppSignal::Subscription>();

	auto thermometer = std::make_shared<Thermometer>();
	{
		// Move the subscription to the shared subscription
		*sharedSubscription = thermometer->OnTemperatureChanged([&temperatureNotification](double value) { temperatureNotification = value; });

		auto sharedSubscription2 = sharedSubscription;
		auto sharedSubscription1 = sharedSubscription;

		// From here on the two other smart pointer keep the subscription alive
		sharedSubscription.reset();

		thermometer->UpdateTemperature(40);
		EXPECT_DOUBLE_EQ(40.0, temperatureNotification);

		thermometer->UpdateTemperature(-10);
		EXPECT_DOUBLE_EQ(-10.0, temperatureNotification);
	}

	// Subscriptions are gone, so the signals do not call the callbacks
	thermometer->UpdateTemperature(-20);
	EXPECT_DOUBLE_EQ(-10.0, temperatureNotification);
}

TEST(SubscriptionReassignmentTest, CppSignalTest)
{
	auto thermometer = std::make_shared<Thermometer>();

	auto temperatureNotification = 0.0;
	auto subscription = thermometer->OnTemperatureChanged([&temperatureNotification](double value) { temperatureNotification = value; });
		
	// Reuse the Subscription instance for another subscription
	auto freezingDays = 0;
	subscription = thermometer->OnFreezing([&freezingDays]() { freezingDays++; });

	thermometer->UpdateTemperature(-10);
	EXPECT_DOUBLE_EQ(0.0, temperatureNotification);
	EXPECT_EQ(1, freezingDays);
}

TEST(SelfUnsubscriptionInEmissionTest, CppSignalTest)
{
	auto thermometer = std::make_shared<Thermometer>();

	auto temperatureNotification = 0.0;
	CppSignal::Subscription subscription;
	subscription = thermometer->OnTemperatureChanged([&temperatureNotification, &subscription](double value) 
		{
			temperatureNotification = value;
			subscription.Unsubscribe();
		});

	thermometer->UpdateTemperature(-10);
	EXPECT_DOUBLE_EQ(-10.0, temperatureNotification);

	thermometer->UpdateTemperature(20);
	EXPECT_DOUBLE_EQ(-10.0, temperatureNotification);
}

TEST(ParallelEmissionTest, CppSignalTest)
{
	auto thermometer = std::make_shared<Thermometer>();

	std::atomic<int> callbackCount(0);

	CppSignal::Subscription subscription;
	subscription = thermometer->OnTemperatureChanged([&callbackCount](double value)
	{
		callbackCount++;
	});

	const int numberOfIterations = 1000000;

	std::thread thread1([thermometer, numberOfIterations]
	{
		for (int i = 0; i < numberOfIterations; ++i)
			thermometer->UpdateTemperature(10);
	});

	std::thread thread2([thermometer, numberOfIterations]
	{
		for (int i = 0; i < numberOfIterations; ++i)
			thermometer->UpdateTemperature(10);
	});

	thread1.join();
	thread2.join();

	EXPECT_EQ(2*numberOfIterations, callbackCount);
}

TEST(StressTest, CppSignalTest)
{
	auto thermometer = std::make_shared<Thermometer>();

	std::atomic<int> callbackCount(0);
	const int numberOfIterations = 1000000;

	// Subcribe and unsubscribe after each 50 cycle
	std::thread thread1([thermometer, numberOfIterations, &callbackCount]
	{
		CppSignal::Subscription subscription;

		for (int i = 0; i < numberOfIterations; ++i)
		{
			if (i % 100 == 0)
			{
				subscription = thermometer->OnTemperatureChanged([&callbackCount](double value)
				{
					callbackCount++;
				});
			}

			if (i % 100 == 50)
			{
				subscription.Unsubscribe();
			}

			thermometer->UpdateTemperature(10);
		}
	});

	std::thread thread2([thermometer, numberOfIterations, &callbackCount]
	{
		CppSignal::Subscription subscription;

		for (int i = 0; i < numberOfIterations; ++i)
		{
			if (i % 100 == 0)
			{
				subscription = thermometer->OnTemperatureChanged([&callbackCount](double value)
				{
					callbackCount++;
				});
			}

			if (i % 100 == 50)
			{
				subscription.Unsubscribe();
			}

			thermometer->UpdateTemperature(10);
		}
	});

	thread1.join();
	thread2.join();

	ASSERT_LE(numberOfIterations, callbackCount);
}
