Health Glance: four health numbers on your Pebble Time 2, straight from Health Connect

I've been using my PT2 with an Android phone and the thing I kept wanting was my actual
Health Connect data on the watch, not a separate ecosystem. Stock Pebble Health on Android
doesn't read Health Connect, so I wrote the app I wanted and it's now in the app store.

It's four cards. Steps, sleep, heart rate, last workout. One big number on each, the useful
detail underneath, and you swipe or press up/down to move between them. That's the whole
app. I had charts and per-day history in an earlier version and took them all back out,
because I never once read them on a watch. If I wanted to look at a graph I'd get my phone out.

The bit I'm most pleased with: if you've got a chest strap or a watch that broadcasts the
standard Bluetooth heart rate service, the phone reads it and the watch shows your live
pulse. The screen edge beats with it, at your actual rate. Fitbit needs "broadcast heart
rate" turned on in its settings first, and it stops the moment samples go stale instead of
pretending an old number is live.

Two things worth knowing before you try it:

- It needs an Android companion app, sideloaded, because the watch can't talk to a strap or
  to Health Connect by itself. PebbleOS only does GATT services as a server, so the phone
  has to be the one doing the reading. The companion only asks for read permissions and
  there's no account, no server, no analytics. Data goes phone to watch and stops there.
- The watch app is Pebble Time 2 only for now. The layout is built around the 200x228 screen
  and I'd rather not ship something broken to smaller ones.

Store link: https://apps.repebble.com/c74e59961a004ec5ab8c72bd
Companion app + source: https://github.com/jollybully/health-glance

Full disclosure: this is vibe coded slop. Both halves were written with AI agents under my
direction and then tested on real hardware, which is why it works and also why there is no
settings screen.

If you've got a PT2 and Health Connect, I'd mainly like to know whether the swipe paging
feels right and whether any of the four cards shows you something wrong. Screenshots of a
card that disagrees with your phone are the most useful bug report I can get.
