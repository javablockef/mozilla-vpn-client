/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import QtQuick 2.5
import QtQuick.Controls 2.14
import QtGraphicalEffects 1.14
import QtQuick.Layouts 1.14
import Mozilla.VPN 1.0
import "../components"
import "../themes/themes.js" as Theme

VPNFlickable {
    id: vpnFlickable
    property bool vpnIsOff: (VPNController.state === VPNController.StateOff)

    VPNMenu {
        id: menu

        title: qsTrId("vpn.settings.networking")
        isSettingsView: true
    }

    VPNCheckBoxRow {
        id: ipv6

        anchors.top: menu.bottom
        anchors.topMargin: Theme.windowMargin
        width: parent.width - Theme.windowMargin

        labelText: qsTrId("vpn.settings.ipv6")
        subLabelText: qsTrId("vpn.settings.ipv6.description")
        isChecked: (VPNSettings.ipv6Enabled)
        isEnabled: vpnFlickable.vpnIsOff
        showDivider: vpnFlickable.vpnIsOff
        onClicked: {
            if (vpnFlickable.vpnIsOff) {
                VPNSettings.ipv6Enabled = !VPNSettings.ipv6Enabled
            }
       }
    }

    VPNCheckBoxRow {
        id: localNetwork

        anchors.top: ipv6.bottom
        anchors.topMargin: Theme.windowMargin
        width: parent.width - Theme.windowMargin
        visible: VPNFeatureList.localNetworkAccessSupported

        labelText: qsTrId("vpn.settings.lanAccess")
        subLabelText: qsTrId("vpn.settings.lanAccess.description")
        isChecked: (VPNSettings.localNetworkAccess)
        isEnabled: vpnFlickable.vpnIsOff
        showDivider: vpnFlickable.vpnIsOff
        onClicked: {
            if (vpnFlickable.vpnIsOff) {
                VPNSettings.localNetworkAccess = !VPNSettings.localNetworkAccess
            }
       }
    }

    VPNCheckBoxAlert {
        anchors.top: localNetwork.visible ? localNetwork.bottom : ipv6.bottom
        visible: !vpnFlickable.vpnIsOff

        errorMessage: qsTrId("vpn.settings.vpnMustBeOff")
    }

}
