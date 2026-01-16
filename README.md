
# wlhc: Wayland hot corners

https://whynothugo.nl/journal/2024/07/11/introducing-wlhc-wayland-hot-corners/
Copyright (c) 2024, Hugo Osvaldo Barrera hugo@whynothugo.nl
Execute a command when the pointer touches a corner of the screen.

# Requirements

Building wlhc requires:

- wayland-protocols
- wlr-protocols

Running wlhc requires a compositor that implements `wlr_layer_shell_v1`. Once
`ext_layer_shell_v1` is stabilised, it will be used instead.

# Usage

```
wlhc: wayland hot corners

Usage: wlhc [-hDetblr] [-d <delay>] cmd args...

-h: print this help text
-d <delay>: execute after delay milliseconds
-D: debug mode (render a red pixel in the specified corner)
-e: exec into command instead of running it as a child
-t: anchor on a top corner
-b: anchor on a bottom corner
-l: anchor on a left corner
-r: anchor on a right corner
```

The `-e` flag can be used when `wlhc` is the child of a service supervisor. It
can ensure that at most one instance of the specified command (or `wlhc` itself)
is running at any given time.

# Example

Run the launcher `fuzzel` when the pointer hits the upper left corner:

    wlhc -tl fuzzel

Run the terminal `foot` when the pointer hits the upper right corner, but only
if the pointer remains there 1.2 seconds:

    wlhc -tl -d 1200 foot

Lock the screen when the pointer toucher the bottom left corner:

    wlhc -bl swaylock

# Caveats

Clicks on the configured corner won't be passed a client rendering in that
corner; they will be swallowed by `wlhc`. This is due to the way it uses
layer-shell, and cannot be fixed without some additional compositor support.
This is only realistically a problem when using a delay with `-d`.

# Limitations

- On multi-seat configurations (e.g.: a single compositor with multiple seats),
  `whlc` will trigger for pointers associated with any seat.

# Licence

wlhc is licensed under the ISC licence. See LICENCE for details.
