\ @app-name: Forth Squares
\ Define a word, then use it. Words are case-insensitive.
\ Run with:  forth /apps/SQUARES.FTH
: square dup * ;
2 square .
3 square .
4 square .
5 square .
cr
bye
