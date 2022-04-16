// Copyright (c) 2016-2021 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>

#include "event/DynamicEventDB.h"
#include "service/ApplicationManager.h"
#include "util/Logger.h"

#include "DefaultBootSequencer.h"

DefaultBootSequencer::DefaultBootSequencer()
    : AbsBootSequencer()
{
}

DefaultBootSequencer::~DefaultBootSequencer()
{
}

bool DefaultBootSequencer::isFirstUse()
{
	return (!isFileExist("/var/luna/preferences/ran-first-use") ||
            (isFileExist("/var/luna/preferences/ran-first-use") && !isFileExist("/var/luna/preferences/first-use-profile-created")));
}

void DefaultBootSequencer::doBoot()
{
    // apps to be started on boot, with keepAlive
    std::vector<std::string> startupCoreAppsOnBoot = {
        "com.palm.launcher",
        "com.palm.systemui",
        "org.webosports.app.phone"
    };
    std::vector<std::string> startupAppsOnBoot = {
        "com.webos.app.notification",
        "com.webos.app.volume",
        "com.palm.app.email",
        "com.palm.app.calendar"
    };

    if(isFirstUse())
    {
        // do not start email and calendar just yet
        startupAppsOnBoot.pop_back(); // "com.palm.app.calendar"
        startupAppsOnBoot.pop_back(); // "com.palm.app.email"
        // start firstuse
        startupAppsOnBoot.push_back("org.webosports.app.firstuse");
    }

    std::vector<std::string> visibleStartupAppsOnBoot = {
        "com.palm.launcher",
        "org.webosports.app.firstuse"
    };

    /* DefaultBootSequencer is just booting. */
    g_Logger.debugLog(Logger::MSGID_BOOTSEQUENCER, "Start DefaultBootSequencer");

    m_bootManager.init(m_mainLoop, this);

    ApplicationManager::instance()->registerServerStatus(&m_bootManager,
            std::bind(&AbsBootSequencer::onSAMStatusChange, this, std::placeholders::_1));

    m_curBootStatus = BootStatus::BootStatus_normal;
    m_curPowerStatus = PowerStatus::PowerStatus_active;
    m_curBootTarget = BootTarget::BootTarget_hardware;

    int displayCnt = StaticEventDB::instance()->getDisplayCnt();
    g_Logger.debugLog(Logger::MSGID_BOOTSEQUENCER, "Display device count : (%d)", displayCnt);

    proceedCoreBootDone();
    proceedInitBootDone();
    proceedDataStoreInitStart();
    ApplicationManager::instance()->listLaunchPoints(&m_bootManager, EventCoreTimeout::EventCoreTimeout_Max);

    int iDisplay = 0;
    // first, start the core apps (launcher, systemui...) with keepalive
    for (iDisplay=0; iDisplay<displayCnt; ++iDisplay) {
        for (auto &appId: startupCoreAppsOnBoot) {
            bool startVisible = (std::find(visibleStartupAppsOnBoot.begin(), visibleStartupAppsOnBoot.end(), appId)!=visibleStartupAppsOnBoot.end());
            launchTargetApp(appId, startVisible, true, iDisplay); // keepAlive : true
        }
    }

    // then, start some basic apps (calendar, email...) without keepalive, and starting hidden
    for (iDisplay=0; iDisplay<displayCnt; ++iDisplay) {
        for (auto &appId: startupAppsOnBoot) {
            bool startVisible = (std::find(visibleStartupAppsOnBoot.begin(), visibleStartupAppsOnBoot.end(), appId)!=visibleStartupAppsOnBoot.end());
            launchTargetApp(appId, startVisible, false, iDisplay); // keepAlive : false
        }
    }

    proceedMinimalBootDone();
    proceedRestBootDone();
    proceedBootDone();
    ApplicationManager::instance()->running(&m_bootManager, this);

    DynamicEventDB::instance()->triggerEvent(DynamicEventDB::EVENT_BOOT_COMPLETE);
    g_Logger.infoLog(Logger::MSGID_BOOTSEQUENCER, "Bootd's job is done");

}

void DefaultBootSequencer::launchTargetApp(string appId, bool visible, bool keepAlive, int displayId)
{
    Application application;
    application.setAppId(appId);
    application.setVisible(visible);
    application.setDisplayId(displayId);
    
    pbnjson::JValue appParams = pbnjson::Object();
    appParams.put("launchedAtBoot", true);
    if(isFirstUse()) {
        appParams.put("mode", "first-use");
    }
    application.setParams(appParams);

    if (keepAlive)
        application.setKeepAlive(keepAlive);

    for (int i = 0; i < COUNT_LAUNCH_RETRY; i++) {
        if (ApplicationManager::instance()->launch(&m_bootManager, application)) {
            if (visible)
                g_Logger.infoLog(Logger::MSGID_BOOTSEQUENCER, "Launch target app (%s) on foreground", application.getAppId().c_str());
            else
                g_Logger.infoLog(Logger::MSGID_BOOTSEQUENCER, "Launch target app (%s) on background", application.getAppId().c_str());
            break;
        }
        g_Logger.warningLog(Logger::MSGID_BOOTSEQUENCER, "Fail to launch '%s'. Retry...(%d)", application.getAppId().c_str(), i);
    }
}
