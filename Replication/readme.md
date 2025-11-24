Šis projektas įgyvendina **Leader–Follower replikuojamą Key-Value Store** su:

-   🔁 **Raft-style leader election**
-   📡 **WAL replication system**
-   📦 **Leader-based writes (SET/DEL)**
-   🔍 **Follower read replicas (eventually consistent)**
-   🚨 **Automatic failover elections**
-   ↩️ **Client REDIRECT mechanizmu**

---

# ⚙️ **1. Reikalavimai**

-   Linux / WSL / macOS
-   GCC 9+ / Clang 10+
-   C++17
-   pthread
-   4 VPS / 4 vietiniai procesai

---

# 🧱 **2. Kompiliavimas**

Kompiliuojame visus komponentus:

```bash
g++ -std=c++17 -pthread leader.cpp -o leader
g++ -std=c++17 -pthread follower.cpp -o follower
g++ -std=c++17 -pthread client.cpp -o client
g++ -std=c++17 -pthread run.cpp -o run

arba

make all
```

---

# 🔌 **3. Klasterio portų schema**

| Portas                  | Naudotojas            | Paskirtis                                 |
| ----------------------- | --------------------- | ----------------------------------------- |
| **7001, 7003, 7005, …** | Clients → Leader      | SET / GET / DEL operacijos                |
| **7002, 7004, 7006, …** | Leader → Followers    | WAL replikacijos srautas                  |
| **7101, 7102, 7103, …** | Clients → Followers   | Read-only GET (gali būti pasenę duomenys) |
| **8001–8004**           | Cluster control plane | Raft heartbeats, vote requests, elections |

---

# 🚀 **4. Kaip paleisti klasterį (run.cpp)**

Kiekviename node paleidžiame:

### Node 1:

```bash
./run 1
```

### Node 2:

```bash
./run 2
```

### Node 3:

```bash
./run 3
```

### Node 4:

```bash
./run 4
```

**run.cpp automatiškai:**

-   Stebi heartbeats
-   Atlieka rinkimus
-   Paleidžia leader ar follower procesus
-   Prižiūri jų restartą

---

# 🧠 **5. Komandos klientui**

## 📝 **SET key/value (rašoma tik į leader)**

```bash
./client <LeaderIP> 7001 SET <key> <value>
```

Pvz:

```bash
./client 100.93.100.112 7001 SET user01 Kaspa
./client 100.93.100.112 7001 SET user02 Tomas
```

---

## 🔍 **GET (automatiškai seka REDIRECT iš followerių)**

```bash
./client <LeaderIP> 7001 GET <key>
```

---

## ❌ **DELETE key (tik leader)**

```bash
./client 100.93.100.112 7001 DEL user01
```

---

# 📖 **6. Skaitymas iš followerių**

## 6.1. 🎯 Tiesiogiai iš followerio (gali būti stale)

```bash
./client <LeaderIP> 7001 GETFF <key> <FollowerIP:710X>
```

Pvz:

```bash
./client 100.93.100.112 7001 GETFF user02 100.125.32.90:7102
```

---

## 6.2. 🔁 Follower → Leader fallback

1. Jei follower turi duomenį – grąžins iš follower.
2. Jei follower NOT_FOUND – nukreips į leader.

```bash
./client <LeaderIP> 7001 GETFB <key> <FollowerIP:710X>
```

Pvz:

```bash
./client 100.93.100.112 7001 GETFB user03 100.96.196.71:7103
```

---

# 🧪 **7. Testuojame replikaciją**

### 🟢 Įrašome duomenis į leader:

```bash
./client 100.93.100.112 7001 SET balance 500
./client 100.93.100.112 7001 SET city Vilnius
./client 100.93.100.112 7001 SET name Jonas
```

### 🔵 Skaitome iš followerių:

Follower #2:

```bash
./client 100.125.32.90 7102 GET name
```

Follower #3:

```bash
./client 100.96.196.71 7103 GET balance
```

---

# 🔥 **8. Failover testas**

1️⃣ **Išjungiame leader (Node 1):**

```bash
killall leader
```

2️⃣ Laukiame ~3 sekundes.

3️⃣ Klasteris automatiškai išrenka naują leader.

4️⃣ Toliau galime rašyti į naują leader:

```bash
./client <NewLeaderIP> 7001 SET user10 Kirpas
```

5️⃣ Visi followeriai turėtų replikuoti duomenis.

---

# 📁 **9. WAL failai**

Kiekvienas node turi savo WAL:

```
node1.log
node2.log
node3.log
node4.log
```

Kiekviena eilutė:

```
<seq> <op> <key> <value>
```

Pvz:

```
1	SET	user01	Kaspa
2	SET	city	Vilnius
3	DEL	user01
```

Followeriai WAL’ą krauna paleidimo metu ir taiko į atmintį.

---

# 🧩 **10. Read-only serveriai followeriuose**

Kiekvienas followeris turi read-only serverį:

-   follower 1 → port **7101**
-   follower 2 → port **7102**
-   follower 3 → port **7103**
-   follower 4 → port **7104**

Jie palaiko tik:

```
GET <key>
```

Visos kitos komandos → automatinis:

```
REDIRECT <LeaderIP> 7001
```

---

# 🛠️ **11. Logai**

### Leader logai:

```
[INFO] [Leader] 100.93.100.112 7001
[INFO] Leader: listening clients on 7001
[INFO] Leader: listening followers on 7002
```

### Follower logai:

```
[Follower][INFO] sent HELLO 33
[Follower][INFO] trying to connect to leader 100.x.x.x:7002
[Follower][WARN] connect failed, sleeping 4000 ms
```

### Run control plane logai:

```
[node 3] [INFO] became LEADER term 12 with votes=3 (required=2)
```
