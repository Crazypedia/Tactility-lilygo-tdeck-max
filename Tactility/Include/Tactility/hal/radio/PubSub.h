#pragma once

#include <functional>
#include <memory>

namespace tt::hal::radio {

// Proposed PubSub class, can be moved elsewhere
template<class... Ts>
class PubSub
{
public:
    typedef int SubscriptionId;
    using Notifier = std::function<void(Ts...)>;

protected:
    struct Subscription {
        SubscriptionId id;
        std::shared_ptr<Notifier> notifier;
    };

    SubscriptionId lastSubscriptionId = 0;
    std::vector<Subscription> subscriptions;

public:

    PubSub() {}
    virtual ~PubSub() = default;

    SubscriptionId subscribe(Notifier onPublish) {
        subscriptions.push_back({
            .id = ++lastSubscriptionId,
            .notifier = std::make_shared<Notifier>(onPublish)
        });
        return lastSubscriptionId;
    }

    void unsubscribe(SubscriptionId subscriptionId) {
        std::erase_if(subscriptions, [subscriptionId](auto& subscription) { return subscription.id == subscriptionId; });
    }

    void publish(Ts... pargs) {
        for (auto& subscription : subscriptions) {
            (*subscription.notifier)(pargs...);
        }
    }
};

}
