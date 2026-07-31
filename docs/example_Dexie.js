<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="UTF-8">
  <title>Dexie.js IndexedDB JSON R/W Demo</title>
  <script src="js/Dexie.js"></script>
</head>
<body>
<h1>Dexie.js IndexedDB JSON Read/Write Demo</h1>
<p>Using <code>js/Dexie.js</code> to store JSON objects in IndexedDB</p>
<hr>

<h2>1. Write JSON</h2>
<label>Key: <input type="text" id="keyInput" placeholder="Enter key" value="user1"></label><br><br>
<label>JSON Value:</label><br>
<textarea id="jsonInput" rows="5" cols="50">{"name":"Alice","age":25,"email":"alice@example.com"}</textarea><br><br>
<button onclick="saveData()">Save (Write)</button>

<hr>

<h2>2. Read JSON</h2>
<label>Key: <input type="text" id="readKeyInput" placeholder="Enter key" value="user1"></label><br><br>
<button onclick="readData()">Read By Key</button>
<button onclick="readAll()">Read All</button>
<button onclick="deleteData()">Delete By Key</button>
<button onclick="clearAll()">Clear All</button>

<hr>

<h2>3. Output</h2>
<pre id="output" style="background:#1e1e1e;color:#d4d4d4;padding:10px;min-height:150px;max-height:300px;overflow-y:auto;"></pre>
<button onclick="document.getElementById('output').textContent=''">Clear Log</button>

<script>
var db = new Dexie("json_store_db");
db.version(1).stores({ items: "key" });

db.open().then(function() {
  log("Database opened: json_store_db");
}).catch(function(err) {
  log("Error: " + err.message);
});

function log(msg) {
  var el = document.getElementById("output");
  el.textContent += "[" + new Date().toLocaleTimeString() + "] " + msg + String.fromCharCode(10);
  el.scrollTop = el.scrollHeight;
}

async function saveData() {
  var key = document.getElementById("keyInput").value.trim();
  var jsonStr = document.getElementById("jsonInput").value.trim();
  if (!key || !jsonStr) { log("Please enter both key and JSON"); return; }
  try {
    var jsonObj = JSON.parse(jsonStr);
    await db.items.put({ key: key, value: jsonObj });
    log("Write OK: key=" + JSON.stringify(key) + " -> " + JSON.stringify(jsonObj));
  } catch (e) { log("Write failed: " + e.message); }
}

async function readData() {
  var key = document.getElementById("readKeyInput").value.trim();
  if (!key) { log("Please enter a key"); return; }
  try {
    var item = await db.items.get(key);
    if (item) {
      log("Read OK: key=" + JSON.stringify(key) + " -> " + JSON.stringify(item.value, null, 2));
    } else {
      log("Not found: key=" + JSON.stringify(key));
    }
  } catch (e) { log("Read failed: " + e.message); }
}

async function readAll() {
  try {
    var all = await db.items.toArray();
    if (all.length === 0) { log("No data"); return; }
    log("Total " + all.length + " records:");
    all.forEach(function(item) {
      log("  key=" + JSON.stringify(item.key) + " -> " + JSON.stringify(item.value));
    });
  } catch (e) { log("Read failed: " + e.message); }
}

async function deleteData() {
  var key = document.getElementById("readKeyInput").value.trim();
  if (!key) { log("Please enter a key"); return; }
  try {
    await db.items.delete(key);
    log("Deleted: key=" + JSON.stringify(key));
  } catch (e) { log("Delete failed: " + e.message); }
}

async function clearAll() {
  if (!confirm("Delete all data?")) return;
  try {
    await db.items.clear();
    log("All data cleared");
  } catch (e) { log("Clear failed: " + e.message); }
}
</script>
</body>
</html>