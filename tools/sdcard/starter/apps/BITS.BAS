REM @app-name: Binary Lab
10 REM Explore powers of two without overflowing signed integers.
20 LET V = 1
30 FOR N = 0 TO 15
40 PRINT N; V
50 LET V = V * 2
60 NEXT N
70 PRINT "BIT INDEX AND DECIMAL VALUE"
80 END
