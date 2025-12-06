# Nordic Thingy:91 Sensor Deployment and Management Guide

This document outlines the professional, step-by-step process for configuring, programming, and managing the Nordic Semiconductor Thingy:91 sensor, covering SIM activation, firmware flashing, and cloud integration via ThingsBoard.

## 1. Prerequisites

### Hardware
* Nordic Thingy:91 Sensor
* Micro-USB cable
* iBasis IoT SIM card
* J-Link Debugger (If external flashing is required)

### Software
* Visual Studio Code (VS Code)
* nRF Connect for Desktop (Programmer app)
* nRF Connect SDK v2.5.0 (Strictly required for compatibility)

## 2. SIM Card Registration and Installation

Before device connectivity, the iBasis SIM card must be activated via the web portal.

1.  **Locate Credentials**: Find the 18-digit ICCID and PUK code on the included iBasis SIM card.
2.  **Web Portal Activation**: Log in to the iBasis web portal.
    * Navigate to Device Management > SIM Card > Add SIM.
    * Input the ICCID and PUK code and click Activate SIM.
    * *Note on Known Issue*: If the page enters an infinite loading loop, refresh the page, log in again, click Activate, enter user details, and click Activate again.
3.  **Installation**: Insert the activated SIM card into the Thingy:91 device.

## 3. Development Environment Setup

Firmware management is performed using the nRF Connect for VS Code Extension Pack.

1.  **Install Extensions**: Install the nRF Connect for VS Code Extension Pack within VS Code.
2.  **Open Project**: In the nRF Connect extension tab, click Add Existing Application. Select the **Parent Folder** of the project to ensure proper recognition of configuration files (`prj.conf`, `overlays`).
3.  **Install SDK**: In the Manage SDKs section of the extension, install **nRF Connect SDK v2.5.0**.

## 4. Building and Flashing Firmware

### Building the Hex File

1.  **Add Build Configuration**: In the nRF Connect extension panel, click Add Build Configuration.
2.  **Define Settings**:
    * SDK: Select v2.5.0.
    * Board Target: Select `thingy91_nrf9160_ns`.
3.  **Generate Build**: Click Build. The process converts C code to the `zephyr.hex` file. Success is indicated by the absence of red errors in the terminal output.

### Flashing the Device

1.  **Connect**: Connect the Thingy:91 to the computer via USB (and J-Link, if applicable).
2.  **Flash**: Click Flash in the VS Code extension panel. The process takes approximately 15 seconds.
    * *Alternative Method*: The `zephyr.hex` file can also be flashed using the nRF Connect Programmer desktop application.

## 5. ThingsBoard Cloud Integration

### Verification

After flashing, verify the device connection to the cloud.

1.  Log in to ThingsBoard.
2.  Navigate to Entities > Devices and confirm the device status is **Active**.
3.  Access Dashboards to view live telemetry data (Accelerometer, Battery, etc.).

### Scaling Up: Adding a New Device

To integrate a new physical sensor, a unique firmware build is required using its specific Access Token.

1.  **Create Device**: In ThingsBoard, go to Devices > Add Device and assign a unique name (e.g., "Thingy\_02").
2.  **Get Token**: Click the new device and copy the generated **Access Token**.
3.  **Update Code**: Open `prj.conf` in VS Code. Locate and replace the string associated with `CONFIG_CLOUD_THINGSBOARD_ACCESS_TOKEN` with the new Access Token.
4.  **Re-Flash**: Save the file, rebuild the project, and flash the new sensor device.

## 6. Modifying Sensor Logic

Behavioral changes are implemented in `application.c`:

* **Vibration Threshold**: The sensitivity of the sensor can be adjusted by modifying the threshold variable. Increasing the value (e.g., to 0.8) makes the sensor less sensitive.
* **Battery Reporting Interval**: The default interval is approximately 12 minutes. For remote deployments, it is recommended to adjust this to 1 hour to conserve data points. This requires updating the milliseconds value corresponding to the timer interval in the code.

## 7. Data Limits and Subscription Management

### Current Data Usage

* **Transmission Rate**: 1 message per second.
* **Data Points per Message**: 3 (Accelerometer X, Accelerometer Z, Battery).
* **Volume**: Approximately 3,600 messages per hour (excluding battery data).

### API Limits

* **Hourly Limit**: 7,000 messages. **Warning**: Exceeding this rate will result in the device being blocked by ThingsBoard.
* **Monthly Limit (Maker Plan)**: 5,000,000 Messages and 10,000,000 Data Points.

### Subscription Strategy

The current Cloud Maker free trial ends January 4th.

* **1-2 Devices**: The $20/month subscription (10M data points) is suitable, provided total usage remains below the limit.
* **3+ Devices**: Usage may exceed the 10M point limit, necessitating the next subscription tier (100M points). A suggested alternative for up to 4 devices is to utilize two separate $20 plans to manage costs.

**Contact**: For any queries, contact Murali Peddi.
