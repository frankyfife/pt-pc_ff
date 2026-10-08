# Control channel

The control channel lets a tool on the same machine read and change the running game: where the player is, the hover,
gravity and collision, the walk speed, the flashlight, the exposure, a camera of its own, the demo speed and the bodies of
stage entities (shown, hidden, enabled). It was made for P.T. Playground (its `ptport.py`), and anything that speaks a
line protocol over TCP can use it.

It is a fixed set of commands. No command runs code it is sent, reads or writes files, or does anything the commands
below do not say. The same functions are in the game's Lua state as `PtDebug` ([lua_api.md](lua_api.md)).

## Turning it on

Off by default. Either of these turns it on:

| where | how |
| --- | --- |
| `pt.ini`, `[extras]` | `control = 1`; `control_port` is the port (default 27510) |
| command line | `--control <port>`; `--control 0` takes the port from `pt.ini` |

A run without a window (`--headless`) opens it only with `--control`.

`pt.log` says it is listening:

```
info  control: listening on 127.0.0.1:27510; port and token in C:\Users\you\AppData\Roaming\pt-port\pt\control.json
```

If the port is taken, the log says so and the game runs without the channel.

## Who can connect

- Only programs on this machine: the server listens on 127.0.0.1 and nowhere else.
- Only programs that can read `control.json`. The game writes it next to `pt.log` at every start (`%APPDATA%\pt-port\pt\`
  on Windows, `~/.local/share/pt-port/pt/` on Linux), with a new random token, readable by this user only on Linux, and
  removes it when the game closes:

  ```json
  {"port": 27510, "token": "9f2c...e41a", "pid": 1234}
  ```

- At most 4 connections at a time.

A browser page cannot use the channel: its first line would have to be the token.

## Protocol

One command per line, words separated by spaces, lines ending in LF (CR LF works too). Every line gets one answer line.

1. The server greets: `pt-control 1`.
2. The client sends `hello <token>`. A good token is answered with `ok` and an object: `protocol`, `game`, `version`
   and `commands`. Anything else is answered with `err` and the connection closes.
3. Every further line is a command. The answer is `ok <json>` or `err <json string>`, the error message.

```
< pt-control 1
> hello 9f2c...e41a
< ok {"protocol":1,"game":"pt-port","version":"1.0.1","commands":[...]}
> hover 1.2
< ok {"x":-0.84,"y":1.2,"z":6.31,"yaw":3.12,...,"hover":1.2,"gravity":true,"collision":true,"input":true,"walkSpeed":1}
> light lumen 400 r 1 g 0.4 b 0.4
< ok {"innerRange":0.005,"outerRange":5,"temperature":5000,"lumen":400,...}
> gravity maybe
< err "usage: gravity on|off"
```

Commands run between two frames, in the order they arrive, at most 256 per connection and frame. A line may be 64 KiB
long. A command that changes something answers with the new state of what it changed.

## Commands

Positions are world space in metres, the floor of the hallway at y = 0. Angles are radians; `yaw` 0 looks along -z. The
field of view is in degrees, vertical. Switches are `on` or `off` (or `1`/`0`, `true`/`false`).

| command | does | answers |
| --- | --- | --- |
| `help` | | the command list |
| `status` | | `floor`, `floorIndex`, `loop`, `step`, `requestedStep`, `frame`, `time`, `paused`, `streetWalk`, `gamePlus`, `demoSpeed`, `demos` (the ids playing) |
| `stages` | | the loaded stages: `label`, `id`, `package`, `active`, `resident`, `origin` |
| `state` | | the session state as text (what the walkthrough tests compare) |
| `player` | | the feet `x`, `y`, `z`; `yaw`, `pitch` (the view), `foxYaw` (the body), `eye`, `spawned`, `grounded`, `verticalVelocity`, `handyLight`, `hover` (while held), `gravity`, `collision`, `input`, `walkSpeed` |
| `place <x> <y> <z> [yaw]` | puts the feet there; the view keeps its direction unless `yaw` is given. During a hover the hover moves to the new height | `player` |
| `turn <yaw> [pitch]` | turns the view; pitch within 70° up and 55° down | `player` |
| `hover <height>` / `hover here` / `hover off` | holds the feet at that height (walking moves only across), at the current one, or lets go (the player falls back) | `player` |
| `gravity on\|off` | off: no falling, walking off an edge keeps the height; floors and stairs still push up | `player` |
| `collision on\|off` | off: walls and floors no longer stop the player, and gravity is off with it. Below y = -70 (except in the ending and on the street) Lisa takes the player, as she does without this | `player` |
| `input on\|off` | off: the player ignores pad, keyboard and mouse and stands still | `player` |
| `walkspeed <factor>` | the walk speed as a factor of `moveSpeedRate` in `ShParameterTables.lua`, 0.05 to 20 | `player` |
| `light` | | the flashlight: `innerRange`, `outerRange`, `temperature`, `lumen`, `lightSize`, `umbraAngle`, `penumbraAngle`, `attenuationExponent`, `dimmer`, `powerScale`, `color` (`r`, `g`, `b`), `floorTint`, `enable`, `changed` |
| `light <name> <value> ...` | sets those values (the names above, `r`, `g`, `b` for the colour) and `enable on\|off`. An unknown name changes nothing | `light` |
| `light reset` | the values from before the first change | `light` |
| `exposure` | | `row` (the floor's lighting row), `min`, `max`, `compensation` (EV, overrides applied), `floor` (the row's own), `overridden`, `ev` while pinned |
| `exposure <min\|max\|compensation\|ev> <value>\|off ...` | overrides min, max or compensation for every floor (`off` gives it back to the floor); `ev` pins the exposure at that EV (`off` releases it) | `exposure` |
| `exposure reset` | drops the overrides and the pin | `exposure` |
| `camera` | | the camera the frame is drawn from: `x`, `y`, `z`, `yaw`, `pitch`, `roll`, `fov`, `forward`, `held` |
| `camera <x\|y\|z\|yaw\|pitch\|roll\|fov> <value> ... [body on\|off]` | holds the view at a camera of its own, detached from the player, also during demos; values not given stay as the current view has them; pitch within ±1.55, fov 5 to 150. `body off` hides the player's body | `camera` |
| `camera off` | gives the view back to the player | `camera` |
| `demospeed <factor>` | demo playback speed, 0 (the picture holds) to 16; sound already playing is not paused | `status` |
| `entity <stage> <name>` | | the entity: `stage`, `name` (full), `class`, `enable`, `visible`, `geom`, `position`; `err` when not found |
| `entity <stage> <name> <enable\|visible\|geom> on\|off ...` | sets the body's flags as a stage script does: `visible` shows or hides, `geom` its collision, `enable` turns traps and other logic on or off | the entity |
| `entities <stage> [prefix] [limit]` | | the entities whose short name starts with `prefix`, at most `limit` (default 500) |
| `floor <name>` | makes `name` the current floor in the floor table (`f000` ... `f160`, `ending`), as `GameFloorLevel.SetFloorLevel` does; it takes effect at the next floor change | `status` |
| `loop <index>` / `loop <floor> [pass]` | the loop browser: index 0 to 17, or the floor (`f050` with pass 1 or 2, `ending`, `street`). Only in play, in the ending and on the street walk, and in a release build only for loops already unlocked | `status` |

`<stage>` is a stage label from `stages` (`current`, `next`, ...), `<name>` an entity's short name (the part after the
last `|`) or its full name.

Everything set here stays until it is set again or the game closes; a new game session does not reset it. The free
camera (F6) and the photo mode (F7) take the view while they run and give it back to the player when they end; `camera`
takes it again.

## From Python

P.T. Playground's `ptport.py` wraps all of this. Without it:

```python
import json, os, socket

info = json.load(open(os.path.expandvars(r"%APPDATA%\pt-port\pt\control.json")))
s = socket.create_connection(("127.0.0.1", info["port"]))
f = s.makefile("rw", encoding="utf-8", newline="\n")

def ask(line):
    f.write(line + "\n"); f.flush()
    status, _, body = f.readline().rstrip("\n").partition(" ")
    if status != "ok":
        raise RuntimeError(json.loads(body))
    return json.loads(body)

f.readline()                      # pt-control 1
ask("hello " + info["token"])
print(ask("player"))
ask("hover 1.5")
```

## In pt.log

| line | meaning |
| --- | --- |
| `control: listening on 127.0.0.1:<port>; port and token in <file>` | the channel is on |
| `control: cannot listen on 127.0.0.1:<port> (...)` | the port is taken or not allowed; the game runs without the channel |
| `control: connection <n> opened` / `signed in` / `closed` | a tool connected, sent a good hello, went away |
| `control: connection <n> closed: no valid hello` | the first line was not `hello` with the right token |
| `control: connection <n> refused, already 4 clients` | too many connections |
