-- Create the room_readings table
CREATE TABLE room_readings (
  id BIGINT PRIMARY KEY GENERATED ALWAYS AS IDENTITY,
  created_at TIMESTAMPTZ DEFAULT NOW(),
  temperature FLOAT NOT NULL,
  humidity FLOAT NOT NULL,
  battery_v FLOAT,
  device_id TEXT DEFAULT 'esp32_01'
);

-- Enable Row Level Security (RLS)
ALTER TABLE room_readings ENABLE ROW LEVEL SECURITY;

-- Allow anonymous inserts (for the ESP32 using the public anon key)
CREATE POLICY "Allow anon insert" ON room_readings
FOR INSERT WITH CHECK (true);

-- Allow anonymous selects (for the dashboard)
CREATE POLICY "Allow anon select" ON room_readings
FOR SELECT USING (true);
