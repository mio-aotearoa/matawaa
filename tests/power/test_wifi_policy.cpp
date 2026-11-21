#include <cassert>
#include <iostream>

#include "wifipowerpolicy.h"

int main()
{
    WifiPowerPolicy p;
    assert(p.policyEnabled());
    assert(p.idleTimeoutSec() == 300);

    // 1. Charging enables Wi-Fi
    auto d = p.onChargingChanged(true);
    assert(d.wifiAction == WifiPolicyDecision::WifiAction::EnableWifi);
    assert(!d.startIdleTimer);

    // ConnMan confirms Wi-Fi powered on while charging -> no idle timer
    d = p.onWifiPoweredChanged(true);
    assert(d.wifiAction == WifiPolicyDecision::WifiAction::None);
    assert(!d.startIdleTimer);
    assert(!p.isIdleTimerRunning());

    // 2. Unplugging on watchface (no Wi-Fi app active) starts idle timer
    d = p.onChargingChanged(false);
    assert(d.wifiAction == WifiPolicyDecision::WifiAction::None);
    assert(d.startIdleTimer);
    assert(p.isIdleTimerRunning());

    // 3. Idle timer expires on battery -> disables Wi-Fi
    d = p.onIdleTimerExpired();
    assert(d.wifiAction == WifiPolicyDecision::WifiAction::DisableWifi);
    assert(!p.isIdleTimerRunning());
    d = p.onWifiPoweredChanged(false);
    assert(d.wifiAction == WifiPolicyDecision::WifiAction::None);

    // 4. Launching a Wi-Fi-needing app on battery enables Wi-Fi without starting timer
    d = p.onWifiAppActiveChanged(true);
    assert(d.wifiAction == WifiPolicyDecision::WifiAction::EnableWifi);
    d = p.onWifiPoweredChanged(true);
    assert(!d.startIdleTimer);
    assert(!p.isIdleTimerRunning());

    // 5. Closing the Wi-Fi app starts idle timer; reopening cancels it
    d = p.onWifiAppActiveChanged(false);
    assert(d.startIdleTimer);
    assert(p.isIdleTimerRunning());
    d = p.onWifiAppActiveChanged(true);
    assert(d.stopIdleTimer);
    assert(!p.isIdleTimerRunning());

    // 6. Closing again and letting timer expire shuts Wi-Fi off
    d = p.onWifiAppActiveChanged(false);
    assert(d.startIdleTimer);
    d = p.onIdleTimerExpired();
    assert(d.wifiAction == WifiPolicyDecision::WifiAction::DisableWifi);
    p.onWifiPoweredChanged(false);

    // 7. Manual Wi-Fi enable on battery (no Wi-Fi app) starts idle timer; manual disable cancels it
    d = p.onWifiPoweredChanged(true);
    assert(d.startIdleTimer);
    d = p.onWifiPoweredChanged(false);
    assert(d.stopIdleTimer);
    assert(!p.isIdleTimerRunning());

    // 8. Disabling policy stops timer and stops managing Wi-Fi
    p.onWifiPoweredChanged(true);
    assert(p.isIdleTimerRunning());
    d = p.onPolicyEnabledChanged(false);
    assert(d.stopIdleTimer);
    d = p.onIdleTimerExpired();
    assert(d.wifiAction == WifiPolicyDecision::WifiAction::None);
    d = p.onChargingChanged(true);
    assert(d.wifiAction == WifiPolicyDecision::WifiAction::None);

    std::cout << "PASS: WifiPowerPolicy state machine transitions\n";
    return 0;
}
