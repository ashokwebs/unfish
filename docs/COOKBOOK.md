# Volume VI: The Unfish Practical Cookbook & Idiomatic Patterns

> **Document Status**: Production Complete • **Specification Level**: Enterprise & Systems  
> **Target Audience**: Software Engineers, Systems Programmers, Tool Authors, Educators  
> **Related Manuals**: [LANGUAGE_SPEC.md](LANGUAGE_SPEC.md) • [STANDARD_LIBRARY.md](STANDARD_LIBRARY.md) • [CONCURRENCY.md](CONCURRENCY.md) • [SYSTEMS_PROGRAMMING.md](SYSTEMS_PROGRAMMING.md)

---

## Table of Contents

1. [Architectural Principles & Idioms](#1-architectural-principles--idioms)
2. [Recipe 01: Command-Line Flag & Argument Parser](#recipe-01-command-line-flag--argument-parser)
3. [Recipe 02: Streaming File Processing & Line Transformation](#recipe-02-streaming-file-processing--line-transformation)
4. [Recipe 03: Structured JSON Serialization, Validation & Mapping](#recipe-03-structured-json-serialization-validation--mapping)
5. [Recipe 04: Data Transformation Pipelines with `|>` & Comprehensions](#recipe-04-data-transformation-pipelines-with--comprehensions)
6. [Recipe 05: In-Memory LRU Cache with TTL Eviction](#recipe-05-in-memory-lru-cache-with-ttl-eviction)
7. [Recipe 06: Concurrent Producer-Consumer Pipeline with CSP Channels](#recipe-06-concurrent-producer-consumer-pipeline-with-csp-channels)
8. [Recipe 07: Binary Protocol Framing with Byte Buffers](#recipe-07-binary-protocol-framing-with-byte-buffers)
9. [Recipe 08: Custom Binary Data Serialization (Pack / Unpack)](#recipe-08-custom-binary-data-serialization-pack--unpack)
10. [Recipe 09: Finite State Machine (FSM) via Enums & Pattern Matching](#recipe-09-finite-state-machine-fsm-via-enums--pattern-matching)
11. [Recipe 10: Dynamic Dependency Injection & Traits](#recipe-10-dynamic-dependency-injection--traits)
12. [Recipe 11: High-Performance Graph Traversal (BFS & DFS)](#recipe-11-high-performance-graph-traversal-bfs--dfs)
13. [Recipe 12: Matrix Operations & Linear Algebra](#recipe-12-matrix-operations--linear-algebra)
14. [Recipe 13: Deterministic Pseudo-Random Generation & Fisher-Yates Shuffling](#recipe-13-deterministic-pseudo-random-generation--fisher-yates-shuffling)
15. [Recipe 14: Resilient Network Retry with Exponential Backoff](#recipe-14-resilient-network-retry-with-exponential-backoff)
16. [Recipe 15: Mock HTTP Request & Response Message Framing](#recipe-15-mock-http-request--response-message-framing)
17. [Recipe 16: Trie (Prefix Tree) for String Indexing & Autocomplete](#recipe-16-trie-prefix-tree-for-string-indexing--autocomplete)
18. [Recipe 17: Binary Min-Heap Priority Queue](#recipe-17-binary-min-heap-priority-queue)
19. [Recipe 18: Disjoint Set Union (Union-Find) with Path Compression](#recipe-18-disjoint-set-union-union-find-with-path-compression)
20. [Recipe 19: Recursive-Descent Expression Parser & Evaluator](#recipe-19-recursive-descent-expression-parser--evaluator)
21. [Recipe 20: Bitmask Flag Sets with Bitwise Logic](#recipe-20-bitmask-flag-sets-with-bitwise-logic)
22. [Recipe 21: Cooperative Fiber Worker Pool](#recipe-21-cooperative-fiber-worker-pool)
23. [Recipe 22: Event Bus & Observer Pattern with Traits](#recipe-22-event-bus--observer-pattern-with-traits)
24. [Recipe 23: Structured Hierarchical Logging Engine](#recipe-23-structured-hierarchical-logging-engine)
25. [Recipe 24: High-Performance Circular Ring Buffer](#recipe-24-high-performance-circular-ring-buffer)
26. [Recipe 25: Text Processing & Markdown Table Formatter](#recipe-25-text-processing--markdown-table-formatter)
27. [Recipe 26: Ocean Fish School Simulation & Vector Movement](#recipe-26-ocean-fish-school-simulation--vector-movement)
28. [Recipe 27: Exception-Safe Resource Guard with Try-Catch-Finally](#recipe-27-exception-safe-resource-guard-with-try-catch-finally)

---

## 1. Architectural Principles & Idioms

Writing idiomatic Unfish programs leverages four core design philosophies:

1. **Explicit Data Structures over Implicit State**: Favor structs with declared types and explicit method receivers over sprawling loose dictionaries.
2. **Defensive Systems Programming**: When manipulating external or binary data, use bounded `buffer` allocations and explicit endianness primitives (`buffer_read_u32_le`, etc.).
3. **Structured Flow over Exception Sprawl**: Use exceptions (`try/catch/finally`) exclusively for unrecoverable exceptional failures (I/O failures, parse aborts). Use `enum Result: Ok(val), Err(msg)` for expected business logic branches.
4. **Dataflow Readability**: Prefer pipeline chains (`|>`) and list/map comprehensions for multi-step data transformations over deeply nested function calls or mutating procedural loops.

---

## Recipe 01: Command-Line Flag & Argument Parser

Parsing CLI flags, boolean switches, and positional arguments without external libraries:

```unfish
import sys

struct CliConfig:
    input_file: String
    output_file: String
    verbose: Boolean
    port: Number

fn parse_cli_args(args):
    let config = CliConfig("", "out.bin", false, 8080)
    let idx = 0
    let total = len(args)

    while idx < total:
        let arg = args[idx]
        if arg == "-v" or arg == "--verbose":
            config.verbose = true
            idx = idx + 1
        else if arg == "-o" or arg == "--output":
            if idx + 1 < total:
                config.output_file = args[idx + 1]
                idx = idx + 2
            else:
                error("Missing argument for " + arg)
        else if arg == "-p" or arg == "--port":
            if idx + 1 < total:
                config.port = to_number(args[idx + 1])
                idx = idx + 2
            else:
                error("Missing argument for " + arg)
        else if starts_with(arg, "-"):
            error(f"Unknown flag: {arg}")
        else:
            if config.input_file == "":
                config.input_file = arg
            idx = idx + 1
    return config

# Example invocation:
let parsed = parse_cli_args(["--verbose", "-p", "9090", "payload.dat", "-o", "result.bin"])
say f"Target: {parsed.input_file}, Out: {parsed.output_file}, Port: {parsed.port}, Verbose: {parsed.verbose}"
```

---

## Recipe 02: Streaming File Processing & Line Transformation

Reading a log file line by line, filtering entries by severity, and writing transformed metrics to an output file:

```unfish
import fs

fn process_server_logs(input_path, output_path):
    if not fs.exists(input_path):
        say f"Error: log file not found at {input_path}"
        return 0

    let content = fs.read(input_path)
    let lines = split(content, "\n")
    let error_count = 0
    let report_lines = []

    for line in lines:
        let trimmed = trim(line)
        if len(trimmed) == 0:
            continue
        
        if contains(trimmed, "[ERROR]"):
            error_count = error_count + 1
            let formatted = f"CRITICAL INCIDENT #{error_count}: {trimmed}"
            push(report_lines, formatted)

    let output_body = join(report_lines, "\n")
    fs.write(output_path, output_body)
    say f"Processed {len(lines)} lines. Identified {error_count} errors. Report written to {output_path}."
    return error_count
```

---

## Recipe 03: Structured JSON Serialization, Validation & Mapping

Validating untrusted external JSON data into strongly typed domain models:

```unfish
import json

struct UserProfile:
    id: Number
    username: String
    email: String
    roles: Array
    is_active: Boolean

fn validate_and_deserialize(raw_json: String):
    let raw = json.parse(raw_json)
    
    # Validate required fields
    if not has_key(raw, "id") or not has_key(raw, "username") or not has_key(raw, "email"):
        error("Validation error: missing mandatory user keys")
    
    let roles = []
    if has_key(raw, "roles") and type_of(raw["roles"]) == "array":
        roles = raw["roles"]

    let active = true
    if has_key(raw, "is_active"):
        active = raw["is_active"]

    return UserProfile(
        raw["id"],
        trim(raw["username"]),
        lower(trim(raw["email"])),
        roles,
        active
    )

# Run verification
let payload = "{\"id\": 1042, \"username\": \"AdaLovelace \", \"email\": \"ADA@Babbage.ORG\", \"roles\": [\"admin\", \"compiler\"]}"
let profile = validate_and_deserialize(payload)
say f"User {profile.username} ({profile.email}) parsed successfully with {len(profile.roles)} roles."
```

---

## Recipe 04: Data Transformation Pipelines with `|>` & Comprehensions

Using point-free pipelines and list comprehensions to cleanly transform tabular records:

```unfish
let raw_transactions = [
    {"user": "alice", "amount": 120.50, "status": "settled"},
    {"user": "bob",   "amount": -45.00, "status": "pending"},
    {"user": "carol", "amount": 350.00, "status": "settled"},
    {"user": "dave",  "amount": 89.20,  "status": "declined"},
    {"user": "eve",   "amount": 210.00, "status": "settled"}
]

# Step 1: Extract settled transaction amounts greater than 100 via comprehension
let high_value_amounts = [
    item["amount"] for item in raw_transactions 
    if item["status"] == "settled" and item["amount"] > 100
]

say f"High value amounts: {inspect(high_value_amounts)}"

# Step 2: Calculate total and apply fee reduction using pipeline operators
fn apply_tax(amount):
    return amount * 1.08

fn format_currency(amount):
    return f"${round(amount * 100) / 100}"

let total_settled = reduce(high_value_amounts, fn(acc, v): acc + v, 0)
let final_payout = total_settled |> apply_tax |> format_currency

say f"Final settlement with tax: {final_payout}"
```

---

## Recipe 05: In-Memory LRU Cache with TTL Eviction

A lightweight Least-Recently-Used (LRU) cache with maximum capacity and access tracking:

```unfish
struct LruCache:
    capacity: Number
    store: Map
    access_order: Array

    fn get(self, key):
        if not has_key(self.store, key):
            return null
        
        # Move key to the end of access order
        let new_order = []
        for k in self.access_order:
            if k != key:
                push(new_order, k)
        push(new_order, key)
        self.access_order = new_order
        
        return self.store[key]

    fn put(self, key, value):
        if has_key(self.store, key):
            self.store[key] = value
            self.get(key) # Refresh ordering
            return null

        # Evict oldest if full
        if len(self.access_order) >= self.capacity:
            let oldest = self.access_order[0]
            let remaining = []
            for i in range(1, len(self.access_order)):
                push(remaining, self.access_order[i])
            self.access_order = remaining
            delete(self.store, oldest)

        self.store[key] = value
        push(self.access_order, key)
        return null

let cache = LruCache(2, {}, [])
cache.put("a", 100)
cache.put("b", 200)
say f"Access a: {cache.get('a')}" # 'a' is now recent
cache.put("c", 300) # Evicts 'b'
say f"Key b exists: {cache.get('b') != null}" # false
say f"Key a exists: {cache.get('a') != null}" # true
```

---

## Recipe 06: Concurrent Producer-Consumer Pipeline with CSP Channels

Cooperative fibers communicating across bounded channels:

```unfish
let queue = channel(3)
let completion = channel(1)

# Producer fiber
spawn(fn():
    for item in [10, 20, 30, 40, 50]:
        say f"[Producer] Emitting work package: {item}"
        queue.send(item)
        yield()
    queue.send(-1) # Sentinel termination value
)

# Consumer fiber
spawn(fn():
    let running = true
    let sum = 0
    while running:
        let val = queue.recv()
        if val == -1:
            running = false
        else:
            say f"[Consumer] Processing {val}"
            sum = sum + val
        yield()
    completion.send(sum)
)

run_scheduler()
let total = completion.recv()
say f"Pipeline completed. Total work processed: {total}"
```

---

## Recipe 07: Binary Protocol Framing with Byte Buffers

Building a binary network framing packet: `[4 bytes MAGIC | 2 bytes OP | 4 bytes LEN | N bytes BODY | 2 bytes CHECKSUM]`:

```unfish
fn pack_network_frame(opcode, payload_str):
    let body_len = len(payload_str)
    let total_len = 4 + 2 + 4 + body_len + 2
    let buf = buffer(total_len)

    # 1. Magic Header 0x55465348 ('UFSH')
    buffer_write_u32_le(buf, 0, 1430664008)

    # 2. Opcode (u16)
    buffer_write_u16_le(buf, 4, opcode)

    # 3. Payload length (u32)
    buffer_write_u32_le(buf, 6, body_len)

    # 4. Copy payload body
    let body_buf = buffer_from_string(payload_str)
    for i in range(body_len):
        buffer_set(buf, 10 + i, buffer_get(body_buf, i))

    # 5. Simple XOR checksum of body
    let checksum = 0
    for i in range(body_len):
        checksum = bxor(checksum, buffer_get(body_buf, i))
    buffer_write_u16_le(buf, 10 + body_len, checksum)

    return buf

fn unpack_network_frame(buf):
    let magic = buffer_read_u32_le(buf, 0)
    if magic != 1430664008:
        error("Corrupted frame: invalid magic header")

    let opcode = buffer_read_u16_le(buf, 4)
    let body_len = buffer_read_u32_le(buf, 6)

    let body_slice = buffer_slice(buf, 10, body_len)
    let body_str = buffer_to_string(body_slice)

    let received_checksum = buffer_read_u16_le(buf, 10 + body_len)
    let expected_checksum = 0
    for i in range(body_len):
        expected_checksum = bxor(expected_checksum, buffer_get(body_slice, i))

    if received_checksum != expected_checksum:
        error("Checksum verification failure")

    return {"opcode": opcode, "payload": body_str}

let packet = pack_network_frame(101, "PING_COMMAND")
say f"Generated binary packet: {buffer_to_hex(packet)}"
let unpacked = unpack_network_frame(packet)
say f"Unpacked opcode: {unpacked.opcode}, payload: {unpacked.payload}"
```

---

## Recipe 08: Custom Binary Data Serialization (Pack / Unpack)

Serializing arbitrary integer arrays into compact little-endian byte buffers:

```unfish
fn serialize_u32_array(numbers):
    let count = len(numbers)
    # Header: 4 bytes count, followed by count * 4 bytes
    let buf = buffer(4 + count * 4)
    buffer_write_u32_le(buf, 0, count)

    for i in range(count):
        buffer_write_u32_le(buf, 4 + i * 4, numbers[i])
    return buf

fn deserialize_u32_array(buf):
    let count = buffer_read_u32_le(buf, 0)
    let result = []
    for i in range(count):
        let val = buffer_read_u32_le(buf, 4 + i * 4)
        push(result, val)
    return result

let original = [1000, 250000, 42, 987654]
let packed = serialize_u32_array(original)
let restored = deserialize_u32_array(packed)
say f"Restored array matches: {original == restored}"
```

---

## Recipe 09: Finite State Machine (FSM) via Enums & Pattern Matching

Implementing a deterministic parser state machine using Algebraic Data Types:

```unfish
enum State:
    Idle
    ReadingHeader(count)
    StreamingPayload(bytes_left)
    Completed(total_bytes)
    Failed(reason)

struct Machine:
    state: State

    fn transition(self, event):
        match self.state:
            when State.Idle:
                if event == "START":
                    self.state = State.ReadingHeader(0)
            when State.ReadingHeader(count):
                if event == "HEADER_OK":
                    self.state = State.StreamingPayload(128)
                else:
                    self.state = State.Failed("Header malformed")
            when State.StreamingPayload(left):
                if left <= 32:
                    self.state = State.Completed(128)
                else:
                    self.state = State.StreamingPayload(left - 32)
            when State.Completed(total):
                say f"Machine already finished with {total} bytes."
            when State.Failed(reason):
                say f"Machine halted on failure: {reason}"

let fsm = Machine(State.Idle)
fsm.transition("START")
fsm.transition("HEADER_OK")
fsm.transition("CHUNK")
say f"State after chunk: {inspect(fsm.state)}"
```

---

## Recipe 10: Dynamic Dependency Injection & Traits

Defining swappable storage drivers through traits and struct implementations:

```unfish
trait KeyValueStorage:
    fn read(self, key)
    fn write(self, key, value)

struct MemoryStorage:
    data: Map

impl KeyValueStorage for MemoryStorage:
    fn read(self, key):
        if has_key(self.data, key):
            return self.data[key]
        return null
    fn write(self, key, value):
        self.data[key] = value

struct Service:
    storage: KeyValueStorage
    fn save_user(self, user_id, name):
        self.storage.write(user_id, name)
    fn lookup_user(self, user_id):
        return self.storage.read(user_id)

let mem = MemoryStorage({})
let app = Service(mem)
app.save_user("u01", "Grace Hopper")
say f"Retrieved user: {app.lookup_user('u01')}"
```

---

## Recipe 11: High-Performance Graph Traversal (BFS & DFS)

Representing directed graphs with adjacency maps and performing traversals:

```unfish
struct Graph:
    adj: Map

    fn add_edge(self, u, v):
        if not has_key(self.adj, u):
            self.adj[u] = []
        let neighbors = self.adj[u]
        push(neighbors, v)
        self.adj[u] = neighbors

    fn bfs(self, start_node):
        let visited = {}
        let queue = [start_node]
        let order = []
        visited[start_node] = true

        while len(queue) > 0:
            let node = queue[0]
            let next_queue = []
            for i in range(1, len(queue)):
                push(next_queue, queue[i])
            queue = next_queue
            push(order, node)

            if has_key(self.adj, node):
                for neighbor in self.adj[node]:
                    if not has_key(visited, neighbor):
                        visited[neighbor] = true
                        push(queue, neighbor)
        return order

let g = Graph({})
g.add_edge("A", "B")
g.add_edge("A", "C")
g.add_edge("B", "D")
g.add_edge("C", "E")
say f"BFS Traversal: {inspect(g.bfs('A'))}"
```

---

## Recipe 12: Matrix Operations & Linear Algebra

Basic 2D matrix multiplication and transposition:

```unfish
fn matrix_create(rows, cols, initial_val):
    let m = []
    for r in range(rows):
        let row = []
        for c in range(cols):
            push(row, initial_val)
        push(m, row)
    return m

fn matrix_multiply(a, b):
    let rows_a = len(a)
    let cols_a = len(a[0])
    let rows_b = len(b)
    let cols_b = len(b[0])

    if cols_a != rows_b:
        error("Matrix dimension mismatch for multiplication")

    let result = matrix_create(rows_a, cols_b, 0)
    for i in range(rows_a):
        for j in range(cols_b):
            let sum = 0
            for k in range(cols_a):
                sum = sum + a[i][k] * b[k][j]
            result[i][j] = sum
    return result

let mat1 = [
    [1, 2],
    [3, 4]
]
let mat2 = [
    [2, 0],
    [1, 2]
]
say f"Matrix Product: {inspect(matrix_multiply(mat1, mat2))}"
```

---

## Recipe 13: Deterministic Pseudo-Random Generation & Fisher-Yates Shuffling

In-place Fisher-Yates array shuffling using modular random arithmetic:

```unfish
fn shuffle_array(arr):
    let n = len(arr)
    let copy = []
    for x in arr:
        push(copy, x)

    let i = n - 1
    while i > 0:
        # Generate random integer between 0 and i inclusive
        let j = random_int(0, i)
        let temp = copy[i]
        copy[i] = copy[j]
        copy[j] = temp
        i = i - 1
    return copy

let deck = ["♠A", "♠K", "♠Q", "♥A", "♥K", "♥Q"]
let shuffled = shuffle_array(deck)
say f"Shuffled deck: {inspect(shuffled)}"
```

---

## Recipe 14: Resilient Network Retry with Exponential Backoff

Executing a fallible operation with bounded retries and exponential delays:

```unfish
fn execute_with_retry(operation_fn, max_attempts):
    let attempt = 1
    let backoff_ms = 50

    while attempt <= max_attempts:
        try:
            say f"[Attempt {attempt}] Executing fallible operation..."
            return operation_fn()
        catch err:
            say f"  Attempt {attempt} failed: {err}"
            if attempt == max_attempts:
                error(f"Operation aborted after {max_attempts} attempts: {err}")
            
            # Simulate backoff delay
            attempt = attempt + 1
            backoff_ms = backoff_ms * 2

let counter = 0
fn flaky_service():
    counter = counter + 1
    if counter < 3:
        error("Connection reset by peer")
    return "SUCCESS_PAYLOAD"

let res = execute_with_retry(flaky_service, 4)
say f"Result obtained: {res}"
```

---

## Recipe 15: Mock HTTP Request & Response Message Framing

Parsing and formatting raw HTTP/1.1 message wire strings:

```unfish
struct HttpRequest:
    method: String
    path: String
    headers: Map
    body: String

fn parse_http_request(raw_text):
    let lines = split(raw_text, "\r\n")
    if len(lines) == 1:
        lines = split(raw_text, "\n") # Support standard newlines

    let request_line = split(lines[0], " ")
    let method = request_line[0]
    let path = request_line[1]

    let headers = {}
    let line_idx = 1
    while line_idx < len(lines):
        let line = lines[line_idx]
        if len(trim(line)) == 0:
            line_idx = line_idx + 1
            break
        let colon_pos = index_of(line, ":")
        if colon_pos > 0:
            let key = lower(trim(replace(line, colon_pos, "")))
            let parts = split(line, ": ")
            headers[parts[0]] = parts[1]
        line_idx = line_idx + 1

    let body_parts = []
    while line_idx < len(lines):
        push(body_parts, lines[line_idx])
        line_idx = line_idx + 1

    return HttpRequest(method, path, headers, join(body_parts, "\n"))

let raw = "GET /api/v1/status HTTP/1.1\r\nHost: api.unfish.org\r\nUser-Agent: UnfishClient\r\n\r\n"
let req = parse_http_request(raw)
say f"HTTP {req.method} to {req.path} from {req.headers['Host']}"
```

---

## Recipe 16: Trie (Prefix Tree) for String Indexing & Autocomplete

Implementing a fast prefix-search trie using recursive maps:

```unfish
struct TrieNode:
    children: Map
    is_terminal: Boolean

fn trie_create():
    return TrieNode({}, false)

fn trie_insert(root, word):
    let curr = root
    let chs = chars(word)
    for c in chs:
        if not has_key(curr.children, c):
            curr.children[c] = trie_create()
        curr = curr.children[c]
    curr.is_terminal = true

fn trie_has_prefix(root, prefix):
    let curr = root
    let chs = chars(prefix)
    for c in chs:
        if not has_key(curr.children, c):
            return false
        curr = curr.children[c]
    return true

let root = trie_create()
trie_insert(root, "unfish")
trie_insert(root, "universe")
trie_insert(root, "unique")

say f"Has prefix 'uni': {trie_has_prefix(root, 'uni')}" # true
say f"Has prefix 'xyz': {trie_has_prefix(root, 'xyz')}" # false
```

---

## Recipe 17: Binary Min-Heap Priority Queue

Standard binary array heap implementing `insert` and `extract_min`:

```unfish
struct MinHeap:
    data: Array

    fn push(self, val):
        push(self.data, val)
        let idx = len(self.data) - 1
        while idx > 0:
            let parent = floor((idx - 1) / 2)
            if self.data[idx] < self.data[parent]:
                let tmp = self.data[idx]
                self.data[idx] = self.data[parent]
                self.data[parent] = tmp
                idx = parent
            else:
                break

    fn pop_min(self):
        if len(self.data) == 0:
            return null
        let root = self.data[0]
        let last = pop(self.data)
        if len(self.data) > 0:
            self.data[0] = last
            let idx = 0
            let size = len(self.data)
            while true:
                let left = 2 * idx + 1
                let right = 2 * idx + 2
                let smallest = idx

                if left < size and self.data[left] < self.data[smallest]:
                    smallest = left
                if right < size and self.data[right] < self.data[smallest]:
                    smallest = right

                if smallest != idx:
                    let tmp = self.data[idx]
                    self.data[idx] = self.data[smallest]
                    self.data[smallest] = tmp
                    idx = smallest
                else:
                    break
        return root

let heap = MinHeap([])
heap.push(45)
heap.push(12)
heap.push(89)
heap.push(7)

say f"Extract min: {heap.pop_min()}" # 7
say f"Extract min: {heap.pop_min()}" # 12
```

---

## Recipe 18: Disjoint Set Union (Union-Find) with Path Compression

Cycle detection in undirected graphs using DSU:

```unfish
struct Dsu:
    parent: Map

    fn find(self, i):
        if not has_key(self.parent, i):
            self.parent[i] = i
            return i
        if self.parent[i] == i:
            return i
        let root = self.find(self.parent[i])
        self.parent[i] = root # Path compression
        return root

    fn union(self, i, j):
        let root_i = self.find(i)
        let root_j = self.find(j)
        if root_i != root_j:
            self.parent[root_i] = root_j
            return true # Merged
        return false # Already in same set (cycle)

let dsu = Dsu({})
dsu.union(1, 2)
dsu.union(2, 3)
say f"1 and 3 connected: {dsu.find(1) == dsu.find(3)}" # true
say f"Adding edge 1-3 creates cycle: {not dsu.union(1, 3)}" # true
```

---

## Recipe 19: Recursive-Descent Expression Parser & Evaluator

Evaluating mathematical expressions `3 + 4 * (2 - 1)` with operator precedence:

```unfish
struct Evaluator:
    input: String
    pos: Number

    fn peek(self):
        if self.pos < len(self.input):
            return chars(self.input)[self.pos]
        return ""

    fn get_char(self):
        let ch = self.peek()
        self.pos = self.pos + 1
        return ch

    fn skip_whitespace(self):
        while self.peek() == " ":
            self.get_char()

    fn parse_number(self):
        self.skip_whitespace()
        let num_str = ""
        while self.peek() >= "0" and self.peek() <= "9":
            num_str = num_str + self.get_char()
        return to_number(num_str)

    fn parse_factor(self):
        self.skip_whitespace()
        if self.peek() == "(":
            self.get_char() # Eat '('
            let val = self.parse_expr()
            self.skip_whitespace()
            self.get_char() # Eat ')'
            return val
        return self.parse_number()

    fn parse_term(self):
        let left = self.parse_factor()
        self.skip_whitespace()
        while self.peek() == "*" or self.peek() == "/":
            let op = self.get_char()
            let right = self.parse_factor()
            if op == "*":
                left = left * right
            else:
                left = left / right
            self.skip_whitespace()
        return left

    fn parse_expr(self):
        let left = self.parse_term()
        self.skip_whitespace()
        while self.peek() == "+" or self.peek() == "-":
            let op = self.get_char()
            let right = self.parse_term()
            if op == "+":
                left = left + right
            else:
                left = left - right
            self.skip_whitespace()
        return left

fn eval_math(expr):
    let ev = Evaluator(expr, 0)
    return ev.parse_expr()

say f"3 + 4 * (10 - 8) = {eval_math('3 + 4 * (10 - 8)')}" # 11
```

---

## Recipe 20: Bitmask Flag Sets with Bitwise Logic

Managing low-overhead privilege bitmasks:

```unfish
# Define permission bitmasks
let PERM_READ    = shl(1, 0) # 1
let PERM_WRITE   = shl(1, 1) # 2
let PERM_EXECUTE = shl(1, 2) # 4
let PERM_ADMIN   = shl(1, 3) # 8

fn add_perm(mask, perm):
    return bor(mask, perm)

fn remove_perm(mask, perm):
    return band(mask, bnot(perm))

fn has_perm(mask, perm):
    return band(mask, perm) == perm

let user_role = 0
user_role = add_perm(user_role, PERM_READ)
user_role = add_perm(user_role, PERM_WRITE)

say f"Can read: {has_perm(user_role, PERM_READ)}"       # true
say f"Can write: {has_perm(user_role, PERM_WRITE)}"     # true
say f"Can execute: {has_perm(user_role, PERM_EXECUTE)}" # false
```

---

## Recipe 21: Cooperative Fiber Worker Pool

Distributing work items across multiple cooperative fibers:

```unfish
fn make_worker_pool(worker_count, jobs):
    let job_channel = channel(len(jobs))
    let result_channel = channel(len(jobs))

    # Push all jobs into the queue
    for j in jobs:
        job_channel.send(j)
    for w in range(worker_count):
        job_channel.send(-1) # Sentinel per worker

    # Launch worker fibers
    for id in range(worker_count):
        spawn(fn():
            let active = true
            while active:
                let job = job_channel.recv()
                if job == -1:
                    active = false
                else:
                    say f"Worker {id} computing square of {job}"
                    result_channel.send(job * job)
                yield()
        )

    run_scheduler()

    let results = []
    for i in range(len(jobs)):
        push(results, result_channel.recv())
    return results

let squared = make_worker_pool(3, [2, 4, 6, 8, 10])
say f"Worker pool results: {inspect(squared)}"
```

---

## Recipe 22: Event Bus & Observer Pattern with Traits

A decoupling pub-sub event bus:

```unfish
trait EventListener:
    fn on_event(self, event_name, payload)

struct EventBus:
    listeners: Map

    fn subscribe(self, event_name, listener: EventListener):
        if not has_key(self.listeners, event_name):
            self.listeners[event_name] = []
        let list = self.listeners[event_name]
        push(list, listener)
        self.listeners[event_name] = list

    fn publish(self, event_name, payload):
        if has_key(self.listeners, event_name):
            for listener in self.listeners[event_name]:
                listener.on_event(event_name, payload)

struct AuditLogger:
    prefix: String

impl EventListener for AuditLogger:
    fn on_event(self, event_name, payload):
        say f"[{self.prefix}] Event '{event_name}' received: {inspect(payload)}"

let bus = EventBus({})
let audit = AuditLogger("AUDIT")
bus.subscribe("order_created", audit)
bus.publish("order_created", {"id": 901, "total": 149.99})
```

---

## Recipe 23: Structured Hierarchical Logging Engine

Configurable logging with level threshold filtering:

```unfish
let LOG_DEBUG = 10
let LOG_INFO  = 20
let LOG_WARN  = 30
let LOG_ERROR = 40

struct Logger:
    name: String
    level: Number

    fn log(self, level, level_name, msg):
        if level >= self.level:
            say f"[{level_name}] [{self.name}] {msg}"

    fn debug(self, msg):
        self.log(LOG_DEBUG, "DEBUG", msg)

    fn info(self, msg):
        self.log(LOG_INFO, "INFO ", msg)

    fn error(self, msg):
        self.log(LOG_ERROR, "ERROR", msg)

let log = Logger("Compiler", LOG_INFO)
log.debug("Entering pass 1 (skipped)")
log.info("Symbol table generated successfully.")
log.error("Fatal syntax error encountered on line 42.")
```

---

## Recipe 24: High-Performance Circular Ring Buffer

A fixed-size ring buffer using modular indexing:

```unfish
struct RingBuffer:
    capacity: Number
    data: Array
    head: Number
    tail: Number
    count: Number

    fn push_item(self, item):
        self.data[self.tail] = item
        self.tail = (self.tail + 1) % self.capacity
        if self.count < self.capacity:
            self.count = self.count + 1
        else:
            self.head = (self.head + 1) % self.capacity # Overwrite oldest

    fn pop_item(self):
        if self.count == 0:
            return null
        let item = self.data[self.head]
        self.data[self.head] = null
        self.head = (self.head + 1) % self.capacity
        self.count = self.count - 1
        return item

fn ring_buffer_create(capacity):
    let arr = []
    for i in range(capacity):
        push(arr, null)
    return RingBuffer(capacity, arr, 0, 0, 0)

let rb = ring_buffer_create(3)
rb.push_item("A")
rb.push_item("B")
rb.push_item("C")
rb.push_item("D") # Overwrites "A"
say f"Popped: {rb.pop_item()}" # "B"
say f"Popped: {rb.pop_item()}" # "C"
say f"Popped: {rb.pop_item()}" # "D"
```

---

## Recipe 25: Text Processing & Markdown Table Formatter

Formatting array-of-maps records into aligned Markdown tables:

```unfish
fn format_markdown_table(headers, rows):
    # Calculate column widths
    let widths = []
    for h in headers:
        push(widths, len(h))

    for row in rows:
        for i in range(len(headers)):
            let val_str = f"{row[headers[i]]}"
            if len(val_str) > widths[i]:
                widths[i] = len(val_str)

    # Build header row
    let header_cells = []
    let separator_cells = []
    for i in range(len(headers)):
        push(header_cells, pad_end(headers[i], widths[i]))
        push(separator_cells, fill("-", widths[i]))

    let out = ["| " + join(header_cells, " | ") + " |", "|-" + join(separator_cells, "-|-") + "-|"]

    # Build data rows
    for row in rows:
        let cells = []
        for i in range(len(headers)):
            let val_str = f"{row[headers[i]]}"
            push(cells, pad_end(val_str, widths[i]))
        push(out, "| " + join(cells, " | ") + " |")

    return join(out, "\n")

let data = [
    {"Name": "Stack VM",   "Opcodes": 57,  "Parity": "100%"},
    {"Name": "Reg VM",     "Opcodes": 256, "Parity": "100%"},
    {"Name": "Native C99", "Opcodes": 0,   "Parity": "100%"}
]
say format_markdown_table(["Name", "Opcodes", "Parity"], data)
```

---

## Recipe 26: Ocean Fish School Simulation & Vector Movement

Simulating multi-agent flocking, vector displacement, distance to navigational landmarks, and functional stream transformations using `struct`, `fn` lambdas, and higher-order mapping:

```unfish
## 🐠 School of Fish Simulation
## Simulating fish movement, vectors and distance in the ocean!

struct Fish:
    id
    x
    y
    speed

    fn swim(self, dx, dy):
        return Fish(self.id, self.x + dx * self.speed, self.y + dy * self.speed, self.speed)

    fn distance_from_reef(self):
        return sqrt(pow(self.x, 2) + pow(self.y, 2))

let school = [
    Fish(1, 10, 20, 1.5),
    Fish(2, 14, 22, 1.2),
    Fish(3, 8,  19, 1.8)
]

say "🌊 Initial Fish Positions in the Lagoon:"
for f in school:
    say f"Fish #{f.id}: pos=({f.x}, {f.y}) • distance to reef={round(f.distance_from_reef())}"

say "\n🏊 A current pushes the school by (dx=5, dy=3):"
let moved_school = map(school, fn(f): f.swim(5, 3))
for f in moved_school:
    say f"Fish #{f.id}: now at ({f.x}, {f.y}) • new distance={round(f.distance_from_reef())}"
```

---

## Recipe 27: Exception-Safe Resource Guard with Try-Catch-Finally

Guaranteed cleanup of allocated resources, open file descriptors, or transactional state using Unfish's dual-stack exception unwinding engine:

```unfish
import fs

struct FileGuard:
    path
    opened

    fn open(self):
        say f"Opening resource at {self.path}"
        self.opened = true
        return self

    fn close(self):
        say f"Closing and releasing resource at {self.path}"
        self.opened = false

fn process_transaction(file_path):
    let guard = FileGuard(file_path, false)
    guard.open()
    try:
        say "Processing transactional data..."
        if contains(file_path, "corrupt"):
            raise "Corrupted transaction record encountered!"
        say "Transaction processed successfully."
        return "SUCCESS"
    catch err:
        say f"Transaction failed safely: {err}"
        return "ROLLBACK"
    finally:
        guard.close()

say f"Run 1: {process_transaction('data_valid.log')}"
say f"Run 2: {process_transaction('data_corrupt.log')}"
```
