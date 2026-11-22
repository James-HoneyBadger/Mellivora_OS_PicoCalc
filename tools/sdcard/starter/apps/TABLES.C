// @app-name: Times Tables
// Tiny C subset: nested loops printing a 1-8 multiplication table.
int row = 1;
int col = 1;
int value = 0;
print("Times tables 1-8");
while (row <= 8) {
  col = 1;
  while (col <= 8) {
    value = row * col;
    print(value);
    col = col + 1;
  }
  row = row + 1;
}
vars
