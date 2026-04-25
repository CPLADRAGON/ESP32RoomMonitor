# AETHER_OS Deployment & DIY Guide

This guide provides step-by-step instructions for cloning AETHER_OS and deploying it on your own hardware.

## 1. Hardware Requirements

To build the AETHER_OS hardware unit, you will need:
- **MCU**: ESP32 Development Board (e.g., DEVKIT V1)
- **Display**: 0.66" SSD1306 OLED (64x48 resolution, I2C)
- **Sensors**: 
  - DHT11 or DHT22 (Temperature/Humidity)
  - MPU6050 (Acceleration - optional)
  - LDR (Light Dependent Resistor)
- **Indicators**: Common Cathode RGB LED
- **Controls**: 1x Momentary Push Button
- **Resistors**: 
  - 1x 10kΩ (for LDR)
  - 3x 220Ω (for RGB LED)

### Pinout Configuration
| Component | ESP32 Pin | Note |
| :--- | :--- | :--- |
| **OLED SDA** | 21 | I2C Data |
| **OLED SCL** | 22 | I2C Clock |
| **OLED RST** | 16 | Reset Pin |
| **DHT Sensor** | 4 | Data Pin |
| **LDR** | 34 | Analog Input |
| **Button** | 33 | Input Pullup |
| **RGB Red** | 13 | PWM Channel 0 |
| **RGB Green** | 14 | PWM Channel 1 |
| **RGB Blue** | 27 | PWM Channel 2 |

---

## 2. Supabase Backend Setup

AETHER_OS uses Supabase for data storage and the Digital Twin dashboard.

1.  **Create Project**: Sign up at [supabase.com](https://supabase.com) and create a new project.
2.  **Initialize Database**:
    - Go to the **SQL Editor** in the Supabase dashboard.
    - Copy the contents of `supabase_schema.sql` from this repository and run it.
    - This creates the `room_readings` and `device_logs` tables with the necessary RLS (Row Level Security) policies.
3.  **Get API Credentials**:
    - Go to **Project Settings** > **API**.
    - Copy the **Project URL** and the **anon public** API key.

---

## 3. Firmware Configuration

The firmware is managed using **PlatformIO**.

1.  **Clone the Repository**:
    ```bash
    git clone <your-repo-url>
    cd ESP32RoomMonitor/firmware
    ```
2.  **Configure Secrets**:
    - Navigate to `include/secrets.h`.
    - Update the following fields:
      - `WIFI_SSID`: Your WiFi network name.
      - `WIFI_PASSWORD`: Your WiFi password.
      - `SUPABASE_URL`: Your Supabase Project URL.
      - `SUPABASE_KEY`: Your Supabase anon public key.
      - `WEATHER_API_KEY`: Your OpenWeatherMap API key.
3.  **Flash the ESP32**:
    - Open the `firmware` folder in VS Code with the PlatformIO extension.
    - Connect your ESP32 via USB.
    - Click **PlatformIO: Build** followed by **PlatformIO: Upload**.

---

## 4. Dashboard Deployment

The web dashboard is a Next.js application.

1.  **Install Dependencies**:
    ```bash
    cd ../dashboard
    npm install
    ```
2.  **Environment Variables**:
    - Create a `.env.local` file in the `dashboard` directory.
    - Add your Supabase credentials:
      ```env
      NEXT_PUBLIC_SUPABASE_URL=your_project_url
      NEXT_PUBLIC_SUPABASE_ANON_KEY=your_anon_key
      ```
3.  **Run Locally**:
    ```bash
    npm run dev
    ```
4.  **Production Deployment**: 
    - The dashboard is ready to be deployed to **Vercel** or any Next.js compatible host. Simply connect your repository and configure the environment variables in the Vercel dashboard.

---

## 5. Usage

1.  **Power On**: The device will attempt to auto-scan if it was woken up by a timer.
2.  **Manual Trigger**: Press the button to enter the menu.
    - **Short Press**: Cycle through menu items.
    - **Long Press (1.2s)**: Select an item (e.g., MEASURE, CLOCK).
3.  **Sleep**: The device automatically enters deep sleep after the menu timeout to conserve power.
