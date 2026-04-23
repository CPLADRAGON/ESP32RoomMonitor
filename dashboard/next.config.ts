import type { NextConfig } from "next";

const nextConfig: NextConfig = {
  /* Allow access from other devices in the local network */
  allowedDevOrigins: ['192.168.88.10', 'localhost:3000']
};

export default nextConfig;
