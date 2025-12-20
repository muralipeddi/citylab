# Provisioning New Vibration Sensor Nodes (nRF9160)

This documentation provides a step-by-step guide for provisioning, flashing, and registering a new Thingy:91 device into the AWS-based vibration analysis pipeline. This process ensures secure connectivity to AWS IoT Core, data transmission to S3, and visibility within Amazon Athena and QuickSight.

---

##  System Overview 
**System:** AWS IoT Vibration Analysis Pipeline 
* **Mode:** Standard Discovery Mode with Direct Query 

---

##  Phase 1: AWS IoT Core Provisioning

**Goal:** Create a digital identity and security certificates for the device.

1. **Access AWS IoT Core:** Navigate to **Manage > All devices > Things** in the AWS Console.

2. **Create Thing:** Click **Create things > Create single thing > Next**.

3. **Device Naming:** * **Thing Name:** Enter the exact ID (e.g., `thingy_04`).

* > **CRITICAL:** This name must match the `CLIENT_ID` in `prj.conf` and the device name in `aws_transport.c`.

4. **Configure Shadow:** Select **"No shadow"** unless specific logic is required.

5. **Certificates:** Select **"Auto-generate a new certificate"** and click **Next**.

6. **Policy:** Attach the `Thingy91_FullAccess` policy and click **Create thing**.

7. **Download Keys:** Download the **Device Certificate** (`.pem.crt`), **Private Key** (`.pem.key`), and **Amazon Root CA 1**.


---

##  Phase 2: Hardware Certificate Flashing

**Goal:** Load security keys into the nRF9160 modem's secure storage.

1. **Connect Hardware:** Connect the Thingy:91 via USB and ensure it is powered ON.

2. **Launch LTE Link Monitor:** Open the application via nRF Connect for Desktop.

3. **Firmware Check:** Ensure nRF firmware is installed to support AT commands.

4. **Modem Preparation:** In the Serial Monitor, type the command: `AT+CFUN=4`.

5. **Certificate Manager:** * Click the **"Certificate Manager"** button or **"Sec Tag"** tab.
* **Security Tag:** Use `16842753` (standard for this project).
* **Upload:** Provide the CA Certificate, Client Certificate, and Private Key.

6. 
**Flash:** Click **Update Modem / Write** and verify the "Certificates written successfully" log.

---

##  Phase 3: Firmware Configuration

**Goal:** Update firmware to transmit using the new Device ID.

1. **Open SDK:** Load the source code in VS Code (nRF Connect SDK).

2. **Update IDs:**
* **Step A (`prj.conf`):** Locate and edit `CONFIG_AWS_IOT_CLIENT_ID_STATIC="thingy_04"`.
* **Step B (`aws_transport.c`):** Locate `#define AWS_TOPIC "thingy91/thingy91_XX/data"` and replace `XX` with the device number.

3. **Compile & Flash:** Run a pristine build to generate a new `.hex` file and click the **Flash** button.

4. **Optional:** Batch size and battery heartbeats can be modified in `application.c`.

---

##  Phase 4: Deployment & Verification

**Goal:** Ensure data is reaching the S3 Bucket.

1. **Power On:** Turn on the device and monitor logs for `LTE Connected` and `AWS IoT Connected`.

2. **S3 Check:** Allow 2–5 minutes for buffering.

3. **Verify Folder:** Navigate to S3 Console > `my-vibration-data-store` and confirm the `device_id=thingy_04` folder exists.

---

##  Phase 5: Database & Visualization

**Goal:** Register partitions and visualize data.

### Athena (Database Registration)

1. Open **Amazon Athena**.

2. Run the repair command: `MSCK REPAIR TABLE vibration_raw;`.

3. Verify the output confirms new partitions were added.

4. **CSVs:** Data can be downloaded by specifying `device_id` and time range in relevant queries.

### QuickSight (Visualization)

1. Open the **QuickSight Dashboard**.

2. **Reload Page:** Press **F5** to force a fresh query to Athena (required for Direct Query mode).

3. **Verify:** Select the new device from the **"Select Device"** dropdown.

---

##  Appendix: Ongoing Maintenance

Because the system uses **Standard Mode** for reliability, Athena must be manually updated for new time-based folders.

**If the dashboard stops showing current data:**

1. Go to **Athena**.

2. Run: `MSCK REPAIR TABLE vibration_raw;`.

3. Refresh the **QuickSight** browser page.


