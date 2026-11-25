# Run an Assimilation Coin (B0RG) full node

Assimilation Coin (ticker **B0RG**) is a full-node coin. The program you run keeps a copy of the chain and talks to other nodes. You do not need to mine to run a node.

Two ports matter:

- **P2P 19333** is how nodes share blocks. It is safe to let this listen.
- **RPC 19332** is the control port. It stays on localhost. Never expose it to the internet, and do not forward it on your router.

A public seed node is live and accepting connections:

- `pool.shed-p00l.com:19333`
- `174.138.71.205:19333`

Either address is that same seed.

## Windows

Download `b0rg-win64.zip` from the [v1.0.0 release](https://github.com/b0rg-collective/b0rg/releases/tag/v1.0.0) and unzip it.

Windows SmartScreen will warn you, because the program is unsigned. Click **More info**, then **Run anyway**.

- `b0rg-qt.exe` is the desktop wallet. It runs a full node and shows the chain in a window.
- `b0rgd.exe` is the same node with no window. Use it with `b0rg-cli.exe` if you do not want a GUI.

Double-click `b0rg-qt.exe`, or from a Command Prompt in that folder:

```bat
b0rgd.exe
b0rg-cli.exe getblockchaininfo
```

The data folder is `%APPDATA%\B0RG` (usually `C:\Users\YourName\AppData\Roaming\B0RG`). The config file is `b0rg.conf` in that folder. Create the folder and the file if they are not there yet. Paste the config below, save it, then start the program again.

## Linux

Download `b0rg-phase1-linux-x86_64.tar.gz` from the [v1.0.0 release](https://github.com/b0rg-collective/b0rg/releases/tag/v1.0.0) and unpack it. The headless programs are `b0rgd` and `b0rg-cli`. Mark them executable and start the node:

```bash
tar -xzf b0rg-phase1-linux-x86_64.tar.gz
cd b0rg-phase1
chmod +x b0rgd b0rg-cli
./b0rgd -daemon
./b0rg-cli getblockchaininfo
```

The data folder is `~/.b0rg`. The config file is `~/.b0rg/b0rg.conf`. Create that file before the first start if you can, or stop the node, write the file, and start it again.

## Config file

`b0rg.conf` is plain text. Use placeholders for the RPC login and replace both of them. Do not publish the password, and do not add `rpcallowip`.

```
server=1
listen=1
# optional, for explorers; leave it out if you do not need it
txindex=1
addnode=pool.shed-p00l.com:19333
addnode=174.138.71.205:19333
rpcuser=<pick one>
rpcpassword=<pick a long random one>
```

`server=1` turns on RPC so `b0rg-cli` can talk to the node. `listen=1` accepts peer connections on port 19333. The two `addnode` lines point at the live seed. `txindex=1` builds a full transaction index, which explorers need; a normal wallet does not. RPC listens on 127.0.0.1 port 19332 only, as long as you do not set `rpcbind` or `rpcallowip`.

## Check that it is syncing

From the folder that contains `b0rg-cli` (on Windows, `b0rg-cli.exe`):

```bash
b0rg-cli getblockchaininfo
b0rg-cli getconnectioncount
b0rg-cli getpeerinfo
```

`getblockchaininfo` shows `blocks`, `headers`, and `verificationprogress`. While `initialblockdownload` is `true`, the node is still catching up. It is finished when `blocks` matches `headers` and `initialblockdownload` is `false`.

`getconnectioncount` is the number of peers. This build has no `getconnectioninfo` command. `getpeerinfo` lists those peers. You want at least one connection, and the seed `pool.shed-p00l.com` or `174.138.71.205` should appear while you sync.

## Mining address

A mining address is a normal wallet address you give to the pool. On the command line:

```bash
b0rg-cli getnewaddress "mining" legacy
```

That prints a `B...` address. A bech32 address works too (`borg1...`):

```bash
b0rg-cli getnewaddress "mining" bech32
```

If the command says no wallet is loaded, create one and ask again:

```bash
b0rg-cli createwallet "mining"
b0rg-cli getnewaddress "mining" legacy
```

In the GUI wallet, use Receive and copy the address. Write it down. Coins the pool pays are sent there.

## Point a Kadena Blake2s ASIC at the pool

Factory firmware on Kadena Blake2s miners (Antminer KA3, Goldshell KD5, KD6, KD-Box, and similar machines) can mine B0RG. In the miner's pool settings use:

- PROP: `stratum+tcp://pool.shed-p00l.com:3340`
- SOLO: `stratum+tcp://pool.shed-p00l.com:4340`

The username is your wallet address. The password is `x`.

PROP shares the block reward with the pool. SOLO pays the whole block only when your machine finds it. The pool pages are at https://shed-p00l.com/pool/b0rg. The public block explorer is https://explorer.shed-p00l.com.
