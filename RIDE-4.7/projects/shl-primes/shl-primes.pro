{
  "name": "shl-primes",
  // Every Shalimar file has a main(); the target says which is the program.
  "groups": {
    "Sources": ["primes.shl", "numbers.shl"]
  },
  "build": { "target": "primes", "groups": ["Sources"] }
}
