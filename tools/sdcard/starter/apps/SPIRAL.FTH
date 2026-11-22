\ @app-name: Starburst
\ A starburst drawn with the Forth graphics words.
\
\ Words: gr text pset line rect frect circle fcircle color pen! show
\        sound noise nosound
\
\ This Forth has no loops or conditionals, so the picture is drawn by a
\ sequence of words. Each source word presents once, so the drawing
\ appears progressively - which looks deliberate rather than slow.

80 gr

\ Palette: 200 = white sun, 11 = yellow, 12 = blue, 14 = cyan
200 255 240 180 color
11 255 210 60 color
12 80 160 255 color
14 120 255 240 color

\ Outer rays (white)
40 40 40 4 200 line
40 40 76 40 200 line
40 40 40 76 200 line
40 40 4 40 200 line

\ Diagonal rays (blue)
40 40 65 15 12 line
40 40 65 65 12 line
40 40 15 65 12 line
40 40 15 15 12 line

\ Shorter diagonal rays (cyan)
40 40 57 23 14 line
40 40 57 57 14 line
40 40 23 57 14 line
40 40 23 23 14 line

\ Inner ring, then the sun itself
40 40 18 200 circle
40 40 4 11 fcircle
40 40 2 200 fcircle

show

\ A short rising arpeggio as the picture finishes
440 200 sound
200 ms
523 200 sound
200 ms
659 300 sound
400 ms
nosound
