/*
 * Copyright 2013  Bhushan Shah <bhush94@gmail.com>
 * Copyright 2013 Sebastian Kügler <sebas@kde.org>
 * Copyright 2015 Kai Uwe Broulik <kde@privat.broulik.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License or (at your option) version 3 or any later version
 * accepted by the membership of KDE e.V. (or its successor approved
 * by the membership of KDE e.V.), which shall act as a proxy
 * defined in Section 14 of version 3 of the license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

import QtQuick 2.0
import QtQuick.Controls 2.3 as QtControls
import QtQuick.Layouts 1.0 as QtLayouts
import org.kde.plasma.calendar 2.0 as PlasmaCalendar
import org.kde.kquickcontrolsaddons 2.0
import org.kde.kirigami 2.5 as Kirigami

QtLayouts.ColumnLayout {
    id: appearancePage

    signal configurationChanged

    property string cfg_fontFamily
    property alias cfg_boldText: boldCheckBox.checked
    property string cfg_timeFormat: ""
    property alias cfg_italicText: italicCheckBox.checked

    property alias cfg_showLocalTimezone: showLocalTimezone.checked
    property alias cfg_displayTimezoneAsCode: timezoneCodeRadio.checked
    property alias cfg_showSeconds: showSeconds.checked

    property alias cfg_showDate: showDate.checked
    property string cfg_dateFormat: "shortDate"
    property alias cfg_customDateFormat: customDateFormat.text
    property alias cfg_use24hFormat: use24hFormat.currentIndex

    onCfg_fontFamilyChanged: {
        // HACK by the time we populate our model and/or the ComboBox is finished the value is still undefined
        if (cfg_fontFamily) {
            for (var i = 0, j = fontsModel.count; i < j; ++i) {
                if (fontsModel.get(i).value === cfg_fontFamily) {
                    fontFamilyComboBox.currentIndex = i
                    break
                }
            }
        }
    }

    ListModel {
        id: fontsModel
        Component.onCompleted: {
            var arr = [] // use temp array to avoid constant binding stuff
            arr.push({text: i18nc("Use default font", "Default"), value: ""})

            var fonts = Qt.fontFamilies()
            var foundIndex = 0
            for (var i = 0, j = fonts.length; i < j; ++i) {
                arr.push({text: fonts[i], value: fonts[i]})
            }
            append(arr)
        }
    }

    Kirigami.FormLayout {
        QtLayouts.Layout.fillWidth: true

        QtControls.CheckBox {
            id: showDate
            Kirigami.FormData.label: i18n("Information:")
            text: i18n("Show date")
        }

        QtControls.CheckBox {
            id: showSeconds
            text: i18n("Show seconds")
        }

        QtControls.CheckBox {
            id: showLocalTimezone
            text: i18n("Show local time zone")
        }

        Item {
            Kirigami.FormData.isSection: true
        }

        QtLayouts.ColumnLayout {
            Kirigami.FormData.label: i18n("Display time zone as:")
            Kirigami.FormData.buddyFor: timezoneCityRadio

            QtControls.RadioButton {
                id: timezoneCityRadio
                text: i18n("Time zone city")
            }

            QtControls.RadioButton {
                id: timezoneCodeRadio
                text: i18n("Time zone code")
            }
        }

        Item {
            Kirigami.FormData.isSection: true
        }

        QtLayouts.RowLayout {
            QtLayouts.Layout.fillWidth: true
            Kirigami.FormData.label: i18n("Time display:")

            QtControls.ComboBox {
                id: use24hFormat
                model: [
                    i18n("12-Hour"),
                    i18n("Use Region Defaults"),
                    i18n("24-Hour")
                ]
                onCurrentIndexChanged: cfg_use24hFormat = currentIndex
            }

            QtControls.Button {
                visible: KCMShell.authorize("formats.desktop").length > 0
                text: i18n("Change Regional Settings...")
                icon.name: "preferences-desktop-locale"
                onClicked: KCMShell.open("formats.desktop")
            }
        }

        Item {
            Kirigami.FormData.isSection: true
        }

        QtLayouts.RowLayout {
            Kirigami.FormData.label: i18n("Date format:")
            enabled: showDate.checked

            QtControls.ComboBox {
                id: dateFormat
                textRole: "label"
                model: [
                    {
                        'label': i18n("Long Date"),
                        'name': "longDate",
                        format: Qt.SystemLocaleLongDate
                    },
                    {
                        'label': i18n("Short Date"),
                        'name': "shortDate",
                        format: Qt.SystemLocaleShortDate
                    },
                    {
                        'label': i18n("ISO Date"),
                        'name': "isoDate",
                        format: Qt.ISODate
                    },
                    {
                        'label': i18nc("custom date format", "Custom"),
                        'name': "custom"
                    }
                ]
                onCurrentIndexChanged: cfg_dateFormat = model[currentIndex]["name"]

                Component.onCompleted: {
                    for (var i = 0; i < model.length; i++) {
                        if (model[i]["name"] === plasmoid.configuration.dateFormat) {
                            dateFormat.currentIndex = i;
                        }
                    }
                }
            }

            QtControls.Label {
                QtLayouts.Layout.fillWidth: true
                textFormat: Text.PlainText
                text: Qt.formatDate(new Date(), cfg_dateFormat === "custom" ? customDateFormat.text
                                                                            : dateFormat.model[dateFormat.currentIndex].format)
            }
        }

        QtControls.TextField {
            id: customDateFormat
            QtLayouts.Layout.fillWidth: true
            enabled: showDate.checked
            visible: cfg_dateFormat == "custom"
        }

        QtControls.Label {
            text: i18n("<a href=\"http://doc.qt.io/qt-5/qml-qtqml-qt.html#formatDateTime-method\">Time Format Documentation</a>")
            enabled: showDate.checked
            visible: cfg_dateFormat == "custom"
            wrapMode: Text.Wrap
            QtLayouts.Layout.preferredWidth: QtLayouts.Layout.maximumWidth
            QtLayouts.Layout.maximumWidth: units.gridUnit * 16

            onLinkActivated: Qt.openUrlExternally(link)
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.NoButton // We don't want to eat clicks on the Label
                cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
            }
        }

        Item {
            Kirigami.FormData.isSection: true
        }

        QtLayouts.RowLayout {
            QtLayouts.Layout.fillWidth: true

            Kirigami.FormData.label: i18n("Font style:")

            QtControls.ComboBox {
                id: fontFamilyComboBox
                QtLayouts.Layout.fillWidth: true
                currentIndex: 0
                // ComboBox's sizing is just utterly broken
                QtLayouts.Layout.minimumWidth: units.gridUnit * 10
                model: fontsModel
                // doesn't autodeduce from model because we manually populate it
                textRole: "text"

                onCurrentIndexChanged: {
                    var current = model.get(currentIndex)
                    if (current) {
                        cfg_fontFamily = current.value
                        appearancePage.configurationChanged()
                    }
                }
            }

            QtControls.Button {
                id: boldCheckBox
                QtControls.ToolTip {
                    text: i18n("Bold text")
                }
                icon.name: "format-text-bold"
                checkable: true
                Accessible.name: QtControls.ToolTip.text
            }

            QtControls.Button {
                id: italicCheckBox
                QtControls.ToolTip {
                    text: i18n("Italic text")
                }
                icon.name: "format-text-italic"
                checkable: true
                Accessible.name: QtControls.ToolTip.text
            }
        }
    }
    Item {
        QtLayouts.Layout.fillHeight: true
    }

    Component.onCompleted: {
        if (plasmoid.configuration.displayTimezoneAsCode) {
            timezoneCodeRadio.checked = true;
        } else {
            timezoneCityRadio.checked = true;
        }
    }
}
