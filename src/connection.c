/* Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <stdio.h>
#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>
#include <zephyr/net/socket.h>
#include <date_time.h>
#include <zephyr/logging/log.h>
//#include <cJSON.h>

#include "connection.h"
#include "led_control.h"

LOG_MODULE_REGISTER(connection, CONFIG_LOG_DEFAULT_LEVEL);

/* Flow control event identifiers */

/* LTE either is connected or isn't */
#define LTE_CONNECTED			(1 << 1)
#define LTE_DISCONNECTED        (1 << 2)


/* Time either is or is not known. This is only fired once, and is never cleared. */
#define DATE_TIME_KNOWN			(1 << 1)

/* Flow control event objects for waiting for key events. */
static K_EVENT_DEFINE(lte_connection_events);
//static K_EVENT_DEFINE(cloud_connection_events);
static K_EVENT_DEFINE(datetime_connection_events);

//static dev_msg_handler_cb_t general_dev_msg_handler;
/**
 * @brief Notify that LTE connection has been established.
 */
static void notify_lte_connected(void)
{
	k_event_post(&lte_connection_events, LTE_CONNECTED);
}

/**
 * @brief Reset the LTE connection event flag.
 */
static void clear_lte_connected(void)
{
	k_event_set(&lte_connection_events, 0);
}

bool await_lte_connection(k_timeout_t timeout)
{
	LOG_DBG("Awaiting LTE Connection");
	return k_event_wait_all(&lte_connection_events, LTE_CONNECTED, false, timeout) != 0;
}

/**
 * @brief Notify that the current date and time have become known.
 */
static void notify_date_time_known(void)
{
	//k_event_post(&cloud_connection_events, DATE_TIME_KNOWN);
	k_event_post(&datetime_connection_events, DATE_TIME_KNOWN);
}

bool await_date_time_known(k_timeout_t timeout)
{
	//return k_event_wait(&cloud_connection_events, DATE_TIME_KNOWN, false, timeout) != 0;
	return k_event_wait(&datetime_connection_events, DATE_TIME_KNOWN, false, timeout) != 0;
}

/**
 * @brief Handler for date_time events. Used exclusively to detect when we have obtained
 * a valid modem time.
 *
 * @param date_time_evt - The date_time event. Ignored.
 */
static void date_time_evt_handler(const struct date_time_evt *date_time_evt)
{
	if (date_time_is_valid()) {
		notify_date_time_known();
	}
}


/**
 * @brief Handler for LTE events coming from modem.
 *
 * @param evt Events from modem.
 */
static void lte_event_handler(const struct lte_lc_evt *const evt)
{
	switch (evt->type) {
	case LTE_LC_EVT_NW_REG_STATUS:
		LOG_DBG("LTE_EVENT: Network registration status %d, %s", evt->nw_reg_status,
			evt->nw_reg_status == LTE_LC_NW_REG_NOT_REGISTERED ?	  "Not Registered" :
			evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ?	 "Registered Home" :
			evt->nw_reg_status == LTE_LC_NW_REG_SEARCHING ?		       "Searching" :
			evt->nw_reg_status == LTE_LC_NW_REG_REGISTRATION_DENIED ?
									     "Registration Denied" :
			evt->nw_reg_status == LTE_LC_NW_REG_UNKNOWN ?			 "Unknown" :
			evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_ROAMING ?
									      "Registered Roaming" :
			evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_EMERGENCY ?
									    "Registered Emergency" :
			evt->nw_reg_status == LTE_LC_NW_REG_UICC_FAIL ?		       "UICC Fail" :
											 "Invalid");

		if ((evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_HOME) &&
		     (evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING)) {
			/* Clear connected status. */
			clear_lte_connected();

			k_event_post(&lte_connection_events, LTE_DISCONNECTED);
		} else {
			/* Notify we are connected to LTE. */
			notify_lte_connected();
		}

		break;
	case LTE_LC_EVT_PSM_UPDATE:
		LOG_DBG("LTE_EVENT: PSM parameter update: TAU: %d, Active time: %d",
			evt->psm_cfg.tau, evt->psm_cfg.active_time);
		break;
	case LTE_LC_EVT_EDRX_UPDATE: {
		/* This check is necessary to silence compiler warnings by
		 * sprintf when debug logs are not enabled.
		 */
		if (IS_ENABLED(CONFIG_MQTT_MULTI_SERVICE_LOG_LEVEL_DBG)) {
			char log_buf[60];
			ssize_t len;

			len = snprintf(log_buf, sizeof(log_buf),
				"LTE_EVENT: eDRX parameter update: eDRX: %f, PTW: %f",
				evt->edrx_cfg.edrx, evt->edrx_cfg.ptw);
			if (len > 0) {
				LOG_DBG("%s", log_buf);
			}
		}
		break;
	}
	case LTE_LC_EVT_RRC_UPDATE:
		LOG_DBG("LTE_EVENT: RRC mode: %s",
			evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ?
			"Connected" : "Idle");
		break;
	case LTE_LC_EVT_CELL_UPDATE:
		LOG_DBG("LTE_EVENT: LTE cell changed: Cell ID: %d, Tracking area: %d",
			evt->cell.id, evt->cell.tac);
		break;
	case LTE_LC_EVT_LTE_MODE_UPDATE:
		LOG_DBG("LTE_EVENT: Active LTE mode changed: %s",
			evt->lte_mode == LTE_LC_LTE_MODE_NONE ? "None" :
			evt->lte_mode == LTE_LC_LTE_MODE_LTEM ? "LTE-M" :
			evt->lte_mode == LTE_LC_LTE_MODE_NBIOT ? "NB-IoT" :
			"Unknown");
		break;
	case LTE_LC_EVT_MODEM_EVENT:
		LOG_DBG("LTE_EVENT: Modem domain event, type: %s",
			evt->modem_evt == LTE_LC_MODEM_EVT_LIGHT_SEARCH_DONE ?
				"Light search done" :
			evt->modem_evt == LTE_LC_MODEM_EVT_SEARCH_DONE ?
				"Search done" :
			evt->modem_evt == LTE_LC_MODEM_EVT_RESET_LOOP ?
				"Reset loop detected" :
			evt->modem_evt == LTE_LC_MODEM_EVT_BATTERY_LOW ?
				"Low battery" :
			evt->modem_evt == LTE_LC_MODEM_EVT_OVERHEATED ?
				"Modem is overheated" :
				"Unknown");
		break;
	default:
		break;
	}
}


/**
 * @brief Set up the modem library.
 *
 * @return int - 0 on success, otherwise a negative error code.
 */
static int setup_modem(void)
{
	int ret;

	/*
	 * If there is a pending modem delta firmware update stored, nrf_modem_lib_init will
	 * attempt to install it before initializing the modem library, and return a
	 * positive value to indicate that this occurred. This code can be used to
	 * determine whether the update was successful.
	 */
	ret = nrf_modem_lib_init();

	if (ret < 0) {
		LOG_ERR("Modem library initialization failed, error: %d", ret);
		return ret;
	} else if (ret == NRF_MODEM_DFU_RESULT_OK) {
		LOG_DBG("Modem library initialized after "
			"successful modem firmware update.");
	} else if (ret > 0) {
		LOG_ERR("Modem library initialized after "
			"failed modem firmware update, error: %d", ret);
	} else {
		LOG_DBG("Modem library initialized.");
	}

	/* Register to be notified when the modem has figured out the current time. */
	date_time_register_handler(date_time_evt_handler);

	return 0;
}




/**
 * @brief Set up LTE and start trying to connect.
 *
 * Must be called AFTER setup_cloud to ensure proper operation of FOTA.
 * @return int - 0 on success, otherwise a negative error code.
 */
static int setup_lte(void)
{
	int err;

	/* Perform Configuration */
	if (IS_ENABLED(CONFIG_POWER_SAVING_MODE_ENABLE)) {
		/* Requesting PSM before connecting allows the modem to inform
		 * the network about our wish for certain PSM configuration
		 * already in the connection procedure instead of in a separate
		 * request after the connection is in place, which may be
		 * rejected in some networks.
		 */
		LOG_INF("Requesting PSM mode");

		err = lte_lc_psm_req(true);
		if (err) {
			LOG_ERR("Failed to set PSM parameters, error: %d", err);
			return err;
		} else {
			LOG_INF("PSM mode requested");
		}
	}

	/* Modem events must be enabled before we can receive them. */
	err = lte_lc_modem_events_enable();
	if (err) {
		LOG_ERR("lte_lc_modem_events_enable failed, error: %d", err);
		return err;
	}

	/* Init the modem, and start keeping an active connection.
	 * Note that if connection is lost, the modem will automatically attempt to
	 * re-establish it after this call.
	 */
	LOG_INF("Starting connection to LTE network...");
	err = lte_lc_init_and_connect_async(lte_event_handler);
	if (err) {
		LOG_ERR("Modem could not be configured, error: %d", err);
		return err;
	}

	return 0;
}


void connection_management_thread_fn(void)
{
    // Change: Set to FAILURE (Solid Red) on startup
    long_led_pattern(LED_FAILURE);

    /* Enable the modem */
    LOG_INF("Setting up modem...");
    if (setup_modem()) {
        LOG_ERR("Fatal: Modem setup failed");
        long_led_pattern(LED_FAILURE); // This is already correct
        return;
    }

    /* Set up LTE and start trying to connect. */
    LOG_INF("Setting up LTE...");
    if (setup_lte()) {
        LOG_ERR("Fatal: LTE setup failed");
        long_led_pattern(LED_FAILURE); // This is already correct
        return;
    }

    LOG_INF("Connecting to LTE network. This may take several minutes...");
    while (true) {
        /* Wait for LTE to become connected (or re-connected if connection was lost). */
        LOG_INF("Waiting for connection to LTE network...");

        // Change: Set to FAILURE (Solid Red) while waiting for connection
        long_led_pattern(LED_FAILURE);

        (void)await_lte_connection(K_FOREVER);
        LOG_INF("Connected to LTE network");

        // Change: Set to DISABLED (Off) once connected
        long_led_pattern(LED_DISABLED);

        /* Clear the disconnected flag in case it was set while connecting */
        k_event_set_masked(&lte_connection_events, 0, LTE_DISCONNECTED);

        /* Wait for disconnection */
        (void)k_event_wait(&lte_connection_events, LTE_DISCONNECTED, false, K_FOREVER);
        LOG_INF("Disconnected from LTE network. Re-connecting...");
        /* Loop repeats, will set LED back to FAILURE (Red) at the top of the loop */
    }
}