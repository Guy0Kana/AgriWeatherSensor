const corsHeaders = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers": "authorization, x-client-info, apikey, content-type",
  "Access-Control-Allow-Methods": "POST, OPTIONS",
  "Content-Type": "application/json",
};

const selectedColumns =
  "id,device_id,recorded_at,device_time,air_temp,humidity,soil_raw,soil_pct,iv,data";

type JsonRecord = Record<string, unknown>;

class DecryptionError extends Error {}

function isRecord(value: unknown): value is JsonRecord {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function jsonResponse(body: JsonRecord, status = 200): Response {
  return new Response(JSON.stringify(body), { status, headers: corsHeaders });
}

function isNullableFiniteNumber(value: unknown): value is number | null {
  return value === null || (typeof value === "number" && Number.isFinite(value));
}

function decodeBase64(value: string): Uint8Array {
  const binary = atob(value);
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
  return bytes;
}

function withoutCiphertext(row: JsonRecord): JsonRecord {
  const { iv: _iv, data: _data, ...plainRow } = row;
  return plainRow;
}

async function decryptReading(
  row: JsonRecord,
  key: CryptoKey,
): Promise<JsonRecord> {
  const iv = row.iv;
  const data = row.data;
  if (iv == null && data == null) return withoutCiphertext(row);
  if (
    typeof iv !== "string" ||
    typeof data !== "string" ||
    typeof row.device_id !== "string"
  ) {
    throw new DecryptionError(`Reading ${String(row.id)} has incomplete encryption fields`);
  }

  try {
    const plaintext = await crypto.subtle.decrypt(
      {
        name: "AES-GCM",
        iv: decodeBase64(iv),
        additionalData: new TextEncoder().encode(row.device_id),
        tagLength: 128,
      },
      key,
      decodeBase64(data),
    );
    const payload: unknown = JSON.parse(new TextDecoder().decode(plaintext));
    if (
      !isRecord(payload) ||
      !isNullableFiniteNumber(payload.t) ||
      !isNullableFiniteNumber(payload.h) ||
      !isNullableFiniteNumber(payload.r) ||
      !isNullableFiniteNumber(payload.p)
    ) {
      throw new DecryptionError(`Reading ${String(row.id)} has an invalid payload`);
    }

    return {
      ...withoutCiphertext(row),
      air_temp: payload.t,
      humidity: payload.h,
      soil_raw: payload.r,
      soil_pct: payload.p,
    };
  } catch (error) {
    if (error instanceof DecryptionError) throw error;
    throw new DecryptionError(`Reading ${String(row.id)} could not be decrypted`);
  }
}

Deno.serve(async (request: Request): Promise<Response> => {
  if (request.method === "OPTIONS") {
    return new Response("ok", { headers: corsHeaders });
  }
  if (request.method !== "POST") {
    return jsonResponse({ error: "Method not allowed" }, 405);
  }

  const authorization = request.headers.get("authorization");
  const apiKey = request.headers.get("apikey");
  if (!authorization?.startsWith("Bearer ") || !apiKey) {
    return jsonResponse({ error: "Authentication required" }, 401);
  }

  const supabaseUrl = Deno.env.get("SUPABASE_URL");
  const keyHex = Deno.env.get("AES_KEY_HEX");
  if (!supabaseUrl || !keyHex || !/^[0-9a-fA-F]{32}$/.test(keyHex)) {
    console.error("SUPABASE_URL or a valid AES_KEY_HEX secret is missing");
    return jsonResponse({ error: "Decryption service is not configured" }, 500);
  }

  let input: unknown;
  try {
    input = await request.json();
  } catch {
    return jsonResponse({ error: "Request body must be valid JSON" }, 400);
  }
  if (!isRecord(input)) {
    return jsonResponse({ error: "Request body must be a JSON object" }, 400);
  }

  const afterId = input.afterId ?? null;
  const limit = input.limit;
  if (
    (afterId !== null &&
      (typeof afterId !== "number" ||
        !Number.isSafeInteger(afterId) ||
        afterId < 0)) ||
    typeof limit !== "number" ||
    !Number.isInteger(limit) ||
    limit < 1 ||
    limit > 500
  ) {
    return jsonResponse({ error: "Invalid pagination parameters" }, 400);
  }

  try {
    const userResponse = await fetch(`${supabaseUrl}/auth/v1/user`, {
      headers: { apikey: apiKey, Authorization: authorization },
    });
    if (userResponse.status === 401 || userResponse.status === 403) {
      return jsonResponse({ error: "Session expired" }, 401);
    }
    if (!userResponse.ok) {
      console.error("Supabase authentication check failed:", userResponse.status);
      return jsonResponse({ error: "Could not verify session" }, 502);
    }

    const query = new URLSearchParams({
      select: selectedColumns,
      order: afterId === null ? "id.desc" : "id.asc",
      limit: String(limit),
    });
    if (afterId !== null) query.set("id", `gt.${afterId}`);

    const readingsResponse = await fetch(
      `${supabaseUrl}/rest/v1/readings?${query.toString()}`,
      {
        headers: {
          apikey: apiKey,
          Authorization: authorization,
          Accept: "application/json",
        },
      },
    );
    if (!readingsResponse.ok) {
      console.error("Supabase readings query failed:", readingsResponse.status);
      return jsonResponse({ error: "Could not read sensor data" }, 502);
    }

    const rows: unknown = await readingsResponse.json();
    if (!Array.isArray(rows) || !rows.every(isRecord)) {
      console.error("Supabase returned an invalid readings response");
      return jsonResponse({ error: "Could not read sensor data" }, 502);
    }

    const keyBytes = Uint8Array.from(
      keyHex.match(/../g) ?? [],
      (byte) => Number.parseInt(byte, 16),
    );
    const cryptoKey = await crypto.subtle.importKey(
      "raw",
      keyBytes,
      { name: "AES-GCM" },
      false,
      ["decrypt"],
    );
    const orderedRows = afterId === null ? rows.reverse() : rows;
    const decryptedRows = await Promise.all(
      orderedRows.map((row) => decryptReading(row, cryptoKey)),
    );
    return jsonResponse({ rows: decryptedRows });
  } catch (error) {
    if (error instanceof DecryptionError) {
      console.error(error.message);
      return jsonResponse({ error: "One or more readings could not be decrypted" }, 422);
    }
    console.error("decrypt-readings failed:", error);
    return jsonResponse({ error: "Internal server error" }, 500);
  }
});
