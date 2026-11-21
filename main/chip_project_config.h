/*
 * CHIP project configuration overrides.
 *
 * CHIPConfig.h includes this file (CHIPConfig.h:59) ahead of every one of its
 * own `#ifndef` defaults, so anything defined here wins without patching the
 * SDK. esp_matter's CMakeLists resolves CONFIG_CHIP_PROJECT_CONFIG against the
 * project root and pushes it onto the Matter component as a PUBLIC
 * -DCHIP_PROJECT_CONFIG_INCLUDE / -DSYSTEM_PROJECT_CONFIG_INCLUDE pair. The
 * hook-up is CONFIG_CHIP_PROJECT_CONFIG in sdkconfig.defaults; without it this
 * file is compiled by nothing and silently does nothing.
 *
 */

#pragma once

/* ---------------------------------------------------------------------------
 * Subscription resumption retry budget.
 *
 * This bounds how long a rebooted ICD can be invisible to its controller.
 *
 * With CONFIG_ENABLE_PERSIST_SUBSCRIPTIONS=y (which sets both
 * CHIP_CONFIG_PERSIST_SUBSCRIPTIONS and CHIP_CONFIG_SUBSCRIPTION_TIMEOUT_
 * RESUMPTION), a node that boots holding a persisted subscription will NOT send
 * an ICD Check-In message to that subscriber.
 *
 * The reasoning upstream is that resuming the subscription serves the same
 * purpose as a check-in. It does right up until the resumption itself fails,
 * and then the Check-In message is the ONLY thing that would tell the
 * controller to establish a fresh CASE session. Until the persisted entry is
 * deleted, the node is unreachable: the controller keeps retransmitting on the
 * pre-reboot session and the node drops every one of them with
 *
 *   Data received on an unknown session (LSID=%d). Dropping it!
 *
 * ...which SessionManager.cpp:878 answers with nothing at all, so the
 * controller never learns to re-CASE. The node looks permanently offline.
 *
 * The entry is deleted only once the retry budget is spent
 * (SubscriptionResumptionSessionEstablisher.cpp:147). At the stock index of 10
 * the schedule is MIN_RETRY + Fibonacci(n) * MULTIPLIER for n = 0..10:
 *
 *   5, 10, 10, 15, 20, 30, 45, 70, 110, 175, 280 minutes  =  12.8 hours
 *
 * Note the per-subscription retry count is persisted to NVS on every failure,
 * so that budget is not restarted by a power cycle -- only the delay sequence is.
 *
 * At index 4 the schedule is
 *
 *   5, 10, 10, 15, 20 minutes  =  1 hour, six CASE attempts
 *
 * after which the entry is dropped, check-ins resume, and the controller
 * re-establishes on the next Idle->Active transition: the next published
 * report, or at the latest the next check-in, kIcdIdleModeSec (1 h) away.
 * Worst case ~2 h instead of ~13 h.
 *
 * MAX_RETRY_INTERVAL_SECS has to move with the index. CHIPConfig.h:1797
 * documents the invariant, which the stock (10, 6 h) pair satisfies and a bare
 * index change would break:
 *
 *   MIN_RETRY + Fibonacci(INDEX + 1) * MULTIPLIER  >  MAX_RETRY_INTERVAL
 *   MIN_RETRY + Fibonacci(INDEX)     * MULTIPLIER  <  MAX_RETRY_INTERVAL
 *
 * With MIN_RETRY and MULTIPLIER left at their 300 s defaults and INDEX = 4,
 * Fibonacci(5) = 5 and Fibonacci(4) = 3, so MAX_RETRY_INTERVAL must satisfy
 * 1200 < MAX_RETRY_INTERVAL < 1800. 1500 s sits in the middle of that window.
 *
 * The suppression bites because this is an ICD: a sleeping node cannot be
 * reached until it checks in, so the controller cannot re-establish on its own.
 * ------------------------------------------------------------------------- */
#define CHIP_CONFIG_SUBSCRIPTION_TIMEOUT_RESUMPTION_MAX_FIBONACCI_STEP_INDEX 4
#define CHIP_CONFIG_SUBSCRIPTION_TIMEOUT_RESUMPTION_MAX_RETRY_INTERVAL_SECS 1500
