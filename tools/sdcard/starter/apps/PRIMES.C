// @app-name: Prime Sieve
// Tiny C subset: primes below 60 using an array and while loops.
int n = 60;
int i = 2;
int j = 0;
int a[64];
print("Primes below 60:");
while (i < n) { a[i] = 1; i = i + 1; }
i = 2;
while (i < n) {
  if (a[i] == 1) {
    print(i);
    j = i + i;
    while (j < n) { a[j] = 0; j = j + i; }
  }
  i = i + 1;
}
vars
