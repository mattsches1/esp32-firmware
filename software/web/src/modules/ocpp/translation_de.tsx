/** @jsxImportSource preact */
import { h } from "preact";
let x = {
    "ocpp": {
        "status": {
            "ocpp": "OCPP-Verbindung",
            "connecting": "Verbinde",
            "connected": "Verbunden",
            "error": "Fehler",
            "status": "Status"
        },
        "navbar": {
            "ocpp": "OCPP"
        },
        "content": {
            "ocpp": "OCPP",
            "enable_ocpp": "OCPP aktiviert",
            "enable_ocpp_desc": "Erlaubt dem konfigurierten OCPP-Server diese Wallbox zu steuern",
            "endpoint_url": "Endpoint-URL",
            "endpoint_url_invalid": "Die Endpoint-URL muss mit entweder ws:// (unverschlüsselt) oder wss:// (verschlüsselt) beginnen und darf nicht mit einem / enden.",
            "identity": "Ladepunkt-Identität",
            "enable_auth": "Autorisierung aktiviert",
            "enable_auth_desc": "Sendet das Passwort oder den 40 Zeichen langen Hex-Key zur Autorisierung beim OCPP-Server",
            "pass": "Passwort oder Hex-Key",
            "tls_cert": "TLS-Zertifikat",
            "use_cert_bundle": "Eingebettetes Zertifikatsbundle",

            "reset": "Zurücksetzen",
            "reset_muted": "Setzt den gesamten OCPP-Zustand zurück",
            "reset_title": "OCPP-Zustand zurücksetzen?",
            "reset_title_text": <>Hiermit wird der gesamte OCPP-Zustand zurückgesetzt. <strong>Bisher nicht erfolgreich übertragene Transaktionsdaten gehen verloren</strong>.</>,
            "abort_reset": "Abbrechen",
            "confirm_reset": "Zurücksetzen",
            "reset_failed": "Zurücksetzen des OCPP-Zustands fehlgeschlagen",

            "debug": "Debug",

            "charge_point_state": "Ladepunktzustand",
            "charge_point_status": "Ladepunkt-OCPP-Status",
            "next_profile_eval": "Nächste Ladeprofilauswertung",
            "connector_state": "Konnektorzustand",
            "connector_status": "Konnektor-OCPP-Status",
            "tag_id": "Tag-ID",
            "parent_tag_id": "Tag-Gruppen-ID",
            "tag_expiry_date": "Tag gültig bis",
            "tag_deadline": "Tag-Deadline",
            "cable_deadline": "Kabel-Deadline",
            "txn_id": "Transaktions-ID",
            "txn_confirmed_time": "Transaktion bestätigt seit",
            "txn_start_time": "Transaktions-Startzeit",
            "current": "Ladestrom",
            "txn_with_invalid_id": "Transaktion mit ungültiger ID",
            "unavailable_requested": "Wechsel nach \"nicht verfügbar\" angefordert",
            "message_in_flight_type": "Typ der aktuellen Nachricht",
            "message_in_flight_id": "ID der aktuellen Nachricht",
            "message_in_flight_len": "Länge der aktuellen Nachricht",
            "message_deadline": "Nachrichts-Deadline",
            "txn_msg_retry_deadline": "Transaktions-Nachrichts-Deadline",
            "message_queue_depth": "Nachrichten in Queue",
            "status_queue_depth": "StatusNotifications in Queue",
            "txn_msg_queue_depth": "Transaktions-Nachrichten in Queue",

            "charge_point_state_0": "Starte",
            "charge_point_state_1": "Sende persistente Nachrichten",
            "charge_point_state_2": "Normal",
            "charge_point_state_3": "Server-Registrierung ausstehend",
            "charge_point_state_4": "Server-Registrierung abgelehnt",
            "charge_point_state_5": "Nicht verfügbar",
            "charge_point_state_6": "Fehler",
            "charge_point_state_7": "Soft Reset",
            "charge_point_state_8": "Hard Reset",

            "status_0": "Verfügbar",
            "status_1": "Bereite vor",
            "status_2": "Lade",
            "status_3": "Pausiert durch EV",
            "status_4": "Pausiert durch Wallbox",
            "status_5": "Fertigstellen",
            "status_6": "Reserviert",
            "status_7": "Nicht verfügbar",
            "status_8": "Fehler",
            "status_9": "Keiner",

            "connector_state_0": "Verfügbar",
            "connector_state_1": "Nicht angeschlossen, nicht autorisiert",
            "connector_state_2": "Angeschlossen, nicht autorisiert",
            "connector_state_3": "Nicht angeschlossen, autorisiere",
            "connector_state_4": "Einseitig angeschlossen, autorisiere",
            "connector_state_5": "Angeschlossen, autorisiere",
            "connector_state_6": "Nicht angeschlossen, autorisiert",
            "connector_state_7": "Einseitig angeschlossen, autorisiert",
            "connector_state_8": "Transaktion läuft",
            "connector_state_9": "Autorisiere für Stop",
            "connector_state_10": "Beende. Warte auf Kabeltrennung",
            "connector_state_11": "Beende. Warte auf freie Dose",
            "connector_state_12": "Beende. Warte auf Autorisierung für Kabelfreigabe",
            "connector_state_13": "Beende. Warte auf Kabel-Besitzer-Tag",
            "connector_state_14": "Nicht verfügbar",

            "message_in_flight_type_0": "Authorize",
            "message_in_flight_type_1": "BootNotification",
            "message_in_flight_type_2": "ChangeAvailabilityResponse",
            "message_in_flight_type_3": "ChangeConfigurationResponse",
            "message_in_flight_type_4": "ClearCacheResponse",
            "message_in_flight_type_5": "DataTransfer",
            "message_in_flight_type_6": "DataTransferResponse",
            "message_in_flight_type_7": "GetConfigurationResponse",
            "message_in_flight_type_8": "Heartbeat",
            "message_in_flight_type_9": "MeterValues",
            "message_in_flight_type_10": "RemoteStartTransactionResponse",
            "message_in_flight_type_11": "RemoteStopTransactionResponse",
            "message_in_flight_type_12": "ResetResponse",
            "message_in_flight_type_13": "StartTransaction",
            "message_in_flight_type_14": "StatusNotification",
            "message_in_flight_type_15": "StopTransaction",
            "message_in_flight_type_16": "UnlockConnectorResponse",
            "message_in_flight_type_17": "AuthorizeResponse",
            "message_in_flight_type_18": "BootNotificationResponse",
            "message_in_flight_type_19": "ChangeAvailability",
            "message_in_flight_type_20": "ChangeConfiguration",
            "message_in_flight_type_21": "ClearCache",
            "message_in_flight_type_22": "GetConfiguration",
            "message_in_flight_type_23": "HeartbeatResponse",
            "message_in_flight_type_24": "MeterValuesResponse",
            "message_in_flight_type_25": "RemoteStartTransaction",
            "message_in_flight_type_26": "RemoteStopTransaction",
            "message_in_flight_type_27": "Reset",
            "message_in_flight_type_28": "StartTransactionResponse",
            "message_in_flight_type_29": "StatusNotificationResponse",
            "message_in_flight_type_30": "StopTransactionResponse",
            "message_in_flight_type_31": "UnlockConnector",
            "message_in_flight_type_32": "GetDiagnosticsResponse",
            "message_in_flight_type_33": "DiagnosticsStatusNotification",
            "message_in_flight_type_34": "FirmwareStatusNotification",
            "message_in_flight_type_35": "UpdateFirmwareResponse",
            "message_in_flight_type_36": "GetDiagnostics",
            "message_in_flight_type_37": "DiagnosticsStatusNotificationResponse",
            "message_in_flight_type_38": "FirmwareStatusNotificationResponse",
            "message_in_flight_type_39": "UpdateFirmware",
            "message_in_flight_type_40": "GetLocalListVersionResponse",
            "message_in_flight_type_41": "SendLocalListResponse",
            "message_in_flight_type_42": "GetLocalListVersion",
            "message_in_flight_type_43": "SendLocalList",
            "message_in_flight_type_44": "CancelReservationResponse",
            "message_in_flight_type_45": "ReserveNowResponse",
            "message_in_flight_type_46": "CancelReservation",
            "message_in_flight_type_47": "ReserveNow",
            "message_in_flight_type_48": "ClearChargingProfileResponse",
            "message_in_flight_type_49": "GetCompositeScheduleResponse",
            "message_in_flight_type_50": "SetChargingProfileResponse",
            "message_in_flight_type_51": "ClearChargingProfile",
            "message_in_flight_type_52": "GetCompositeSchedule",
            "message_in_flight_type_53": "SetChargingProfile",
            "message_in_flight_type_54": "TriggerMessageResponse",
            "message_in_flight_type_55": "TriggerMessage",

            "configuration": "Konfiguration"
        },
        "script": {
            "save_failed": "Speichern der OCPP-Einstellungen fehlgeschlagen.",
            "reboot_content_changed": "OCPP-Einstellungen"
        }
    }
}
