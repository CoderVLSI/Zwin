const std = @import("std");

pub fn main() !void {
    // 1. Set up memory allocator
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    // 2. Fetch Gemini API key from environment
    const api_key = std.process.getEnvVarOwned(allocator, "GEMINI_API_KEY") catch |err| {
        std.debug.print("Error: GEMINI_API_KEY environment variable is not set. ({s})\n", .{@errorName(err)});
        std.debug.print("Please set it using: export GEMINI_API_KEY=\"your_key\"\n", .{});
        return;
    };
    defer allocator.free(api_key);

    // 3. Initialize HTTP client
    var client = std.http.Client{ .allocator = allocator };
    defer client.deinit();

    // 4. Construct API Endpoint URL (using gemini-2.5-flash-lite)
    const model = "gemini-2.5-flash-lite";
    const url_str = try std.fmt.allocPrint(allocator, "https://generativelanguage.googleapis.com/v1beta/models/{s}:generateContent?key={s}", .{ model, api_key });
    defer allocator.free(url_str);

    const uri = try std.Uri.parse(url_str);
    std.debug.print("[ZIG AGENT] Connecting to Gemini API...\n", .{});

    // 5. Construct JSON prompt payload
    const payload = "{\"contents\": [{\"parts\": [{\"text\": \"You are a helpful, lightweight AI agent written in the Zig programming language running on a Raspberry Pi Zero 2 W. Introduce yourself in 2 sentences.\"}]}]}";

    // 6. Set up HTTP headers
    var headers = std.http.Headers{ .allocator = allocator };
    defer headers.deinit();
    try headers.append("content-type", "application/json");

    // 7. Open Request
    var req = try client.request(.POST, uri, headers, .{});
    defer req.deinit();

    req.transfer_behavior = .{ .content_length = payload.len };
    try req.start();

    // Send the JSON payload
    try req.writer().writeAll(payload);
    try req.finish();

    // 8. Wait for and read response
    try req.wait();

    std.debug.print("[ZIG AGENT] Response status: {}\n", .{req.response.status});
    if (req.response.status != .ok) {
        std.debug.print("Error: API request failed.\n", .{});
        return;
    }

    // Allocate memory to hold the response body (max 8KB)
    var body_buf: [8192]u8 = undefined;
    const bytes_read = try req.reader().readAll(&body_buf);
    const body = body_buf[0..bytes_read];

    // 9. Parse Response JSON to extract the response text
    // JSON path: candidates[0] -> content -> parts[0] -> text
    var parsed = try std.json.parseFromSlice(std.json.Value, allocator, body, .{});
    defer parsed.deinit();

    const root = parsed.value;
    const candidates = root.object.get("candidates") orelse {
        std.debug.print("Error: Missing 'candidates' in JSON.\n", .{});
        return;
    };
    const first_candidate = candidates.array.items[0];
    const content = first_candidate.object.get("content") orelse {
        std.debug.print("Error: Missing 'content' in JSON.\n", .{});
        return;
    };
    const parts = content.object.get("parts") orelse {
        std.debug.print("Error: Missing 'parts' in JSON.\n", .{});
        return;
    };
    const first_part = parts.array.items[0];
    const text = first_part.object.get("text") orelse {
        std.debug.print("Error: Missing 'text' in JSON.\n", .{});
        return;
    };

    // 10. Print the response
    std.debug.print("\n🤖 AI> {s}\n\n", .{text.string});
}
