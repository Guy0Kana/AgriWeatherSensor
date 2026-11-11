# AgriWeatherSensor
IoT-based agricultural data collection system for smallholder farmers in Busia County, Kenya. ESP32 sensor nodes capture soil and weather data, transmit it to the cloud via MQTT/HTTP, and display it on a live dashboard, forming the data foundation for a future AI advisory system.

## Encrypted dashboard readings

The dashboard calls the `decrypt-readings` Supabase Edge Function, which uses
the `AES_KEY_HEX` Function secret to decrypt encrypted readings for the signed-in
user. The function is in `supabase/functions/decrypt-readings/index.ts`; its
configuration enables JWT verification. Add `AES_KEY_HEX` under **Edge Functions
→ Secrets** in the Supabase Dashboard, then deploy from the repository root with:

```powershell
supabase functions deploy decrypt-readings --project-ref wubufrivwsmjgqjnavew
```

The function queries `readings` using the caller's JWT, so the existing
Row Level Security policies continue to control which readings the user can see.
