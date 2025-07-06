
<img src="docs/apelang.png" alt="apelang" width="200"/>

# Apelang: The Primate Way of Communication

 **Welcome, fellow primates, to Apelang!**  
Before there was *Humanlang* (that fancy language with networking and logic at [humanlang](https://github.com/duongddinh/humanlang)), and even before Apelang itself, there was something **even more prehistoric**, [**DinoLang**](https://github.com/duongddinh/dinolang).

Apelang is a direct **evolution of DinoLang**, a fossil-fuelled toy language from a time when variables were declared with "FOSSIL" and strings were ROARed into the digital void. DinoLang predates Apelang by **several geological epochs**. Apelang is basically a newborn mammal by comparison.

Apelang modernizes Dino's prehistoric grunts into **tribes, trees, and swinging loops**, with structured syntax, string support, modules, error handling, and even data structures. But don’t worry, it's still 99.8% banana-powered.

Apelang is a **prehistoric programming language**.  
Forget your high-level abstractions and complex frameworks. Apelang operates on pure, unfiltered primate instinct. It’s simple, it’s raw, and surprisingly very effective for tasks that involve a lot of swinging, asking, and printing trees.

---

## Features

+ **Primitive Data Types**: `number`, `string`, `boolean`, `nil`
+ **Arithmetic**: `ooh` (add), `aah` (subtract), `eek` (multiply), `ook` (divide)
+ **Control Flow**: `if`, `else`, `swing` (loop), `give` (return)
+ **Functions**: `tribe` blocks with `give`
+ **Input/Output**: `tree` (print), `ask()` (input)
+ **Arrays**: `bunch`es of values: `[1, "banana", true]`
+ **Maps**: `canopy` key-value stores: `{"food": "banana"}`
+ **Modules**: `summon "helpers.ape"` to include external code
+ **Error Handling**: `tumble { ... } catch (err) { ... }`

---

## Getting Started (For the Less Evolved)

You’ll need a **C compiler** (`gcc` or `clang`) and `make`.

```bash
git clone https://github.com/duongddinh/apelang.git
cd apelang
make
````

---

## Global Installation

Make `apeslang` available from anywhere with:

```bash
sudo mv apeslang /usr/local/bin
```

OR, even easier:

```bash
./install.sh
```

The `install.sh` script:

* Builds the `apeslang` binary
* Installs it to `/usr/local/bin`
* Verifies installation

After that, you can use `apeslang` anywhere in your terminal!

---

## How to Speak Ape

### Keywords

* `ape`: Declare a new variable.
* `tree`: Print to console.
* `ask()`: Get user input.
* `if` / `else`: Conditional execution.
* `swing`: Loop.
* `tribe`: Define a function.
* `give`: Return from a function.
* `tumble` / `catch`: Try-catch-style error handling.
* `summon`: Import another `.ape` module file.

### Operators

| Symbol                  | Meaning           |
| ----------------------- | ----------------- |
| `ooh`                   | Addition / Concat |
| `aah`                   | Subtraction       |
| `eek`                   | Multiplication    |
| `ook`                   | Division          |
| `==`                    | Equality          |
| `!=`                    | Inequality        |
| `<` / `<=` / `>` / `>=` | Comparisons       |

---

## Advanced Ape Knowledge

### Bunch (Array)

```ape
ape myBunch = [10, "banana", true]
tree myBunch[1] # Prints: banana
myBunch[0] = 99
tree myBunch
```

### Canopy (Map)

```ape
ape myCanopy = {"name": "Koko", "age": 5}
tree myCanopy["name"]
myCanopy["age"] = 6
```

### Tumble / Catch (Error Handling)

```ape
tumble {
  tree "Trying something risky..."
  ape fail = 10 ooh "banana" # Type error!
} catch (err) {
  tree "Caught an error!"
  tree err
}
```

### Summon (Modules)

```ape
summon "helpers.ape"
tree helperVar
sayHelloFromHelper("Koko")
```

---

## Compile & Run

### Compile a .ape file to Ape Bytecode (.apb):

```bash
apeslang compile hellobanana.ape
```

Output:

```
Compilation successful. 🦍🍌
```

### Run the compiled bytecode:

```bash
apeslang run hellobanana.apb
```

---

## Apelang in The Jungle

### 1. Hello Jungle

```ape
tree "OOH-OOH-AAH-AAH! Hello, Jungle!"
```

---

### 2. Counting Bananas

```ape
ape bananas = 5
swing bananas {
  tree "I have a banana!"
  bananas = bananas aah 1
}
```

---

### 3. If/Else Foraging

```ape
ape weather = "rainy"
if weather == "sunny" {
  tree "Time to forage for berries!"
} else {
  tree "Stay in cave. Too wet for foraging."
}
```

---

### 4. Wise Old Ape (Function)

```ape
tribe wiseWords() {
  tree "Ape who chases two bananas catches none."
  give nil
}
wiseWords()
```

---

### 5. Input

```ape
tree "How many bananas do you have?"
ape myBananas = ask()
tree "You say you have " ooh myBananas ooh " bananas. OOH-OOH-AAH-AAH!"
```

---

## Availability & Compatibility

Apelang runs on:

macOS (Intel or Apple Silicon)
Linux (x86\_64, ARM with GCC)
Windows (via WSL, MinGW, or Cygwin)

To support all platforms:

* Share `.ape` source or precompiled `.apb` files
* Share the `apeslang` binary **built for that OS/CPU**
* Use `install.sh` or `make` for easy builds

---

##  Apelang vs. Humanlang

While Humanlang, with its modern Wi-Fi era, creates advanced networking capabilities and runs on fancy interpreters. Apelang is a much more less grounded language. Apelang is a compiled language, meaning your ape code is translated directly into raw, efficient bytecode that the VM executes. There's no middle-ape interpreter like THE TALKING SNAKE; it's a direct connection to the machine's primal core. This also means, unlike its futuristic descendant, Apelang doesn't concern itself with the like of networking or the internet. Our apes communicate through shouts, gestures. If you want to connect to other apes, you'll have to do it the old-fashioned way: by swinging over to their tree.


###  Apelang vs. Humanlang vs. DinoLang: A Tale of Three Epochs

- **DinoLang** is raw binary driven by integers and yelling.
- **Apelang** builds on DinoLang with loops, scopes, strings, modules, and basic error handling.
- **Humanlang** is the sleek, Wi-Fi-enabled descendent, more evolved, but less hairy.

Each speaks to a different era in evolutionary coding. Choose based on your brain size, tool preference, and number of bananas nearby.

---

##  Known Bugs (a.k.a. “Unpredictable Ape Behavior”)

* Sometimes the compiler throws a banana peel at you. Try again.
* The VM might go on a spontaneous swinging spree. It's not a bug, it's enrichment.
* Error messages are in “Ape-speak” and may require interpretation.

---

## Contribute

Found a bug? Got a new banana idea? Fork and PR. We love more apes in the tribe!

---

## Good luck, and happy swinging! 

```

Let me know if you want this saved to a file, turned into a GitHub README, or translated into Spanish or Markdown-pdf!
```
