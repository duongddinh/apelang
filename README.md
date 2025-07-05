# 🍌 Apelang: The Primate's Programming Paradigm 

🦍Welcome, fellow primates, to Apelang! Before there was Humanlang (you know, that fancy language with all the words and logic at [humanlang](https://github.com/duongddinh/humanlang)), there was Apelang. 
This is where it all began, deep in the digital jungle, with grunts, gestures, and the occasional banana.

Apelang is a prehistoric programming language. Forget your high-level abstractions and complex frameworks; Apelang operates on pure, unadulterated primate instinct. 

It's simple, it's raw, and it's surprisingly effective for tasks that involve a lot of swinging, asking, and printing trees.

## Features
 + Primitive Data Types: We've got numbers (for counting bananas), booleans (for "yes banana" or "no banana"), and strings (for shouting "OOH-OOH-AAH-AAH!").
 + Basic Arithmetic: Add, subtract, multiply, and divide. Because even a primate needs to know how many bananas are left after sharing.
 + Control Flow: if (if banana, then eat), swing (loop, like swinging through trees), and give (return, like giving back a half-eaten banana).
 + Functions (or "Tribes"): Organize your code into reusable "tribes" of instructions. Because a lone ape is a sad ape.
 + Input/Output: tree (print, for showing off your banana count) and ask (for asking if anyone has more bananas).

🐒 Getting Started (for the less evolved)T o get Apelang swinging on your machine, you'll need a C compiler (because even prehistoric languages need a modern tool to come to life).
Clone this ancient repository and Compile the Ape: 

```
git clone https://github.com/duongddinh/apelang.git
cd apelang
make
```

## How to speak Ape
Ready to master the grunts and gestures of Apelang?:

**Keywords (The Primal Commands)**
```ape``` - Declare a new variable. This is how you tell the jungle you've found a new spot for your bananas (or numbers, or true/false statements).
```
ape myFavoriteNumber = 42
ape isHungry = true
ape apeName = "Chimpanzee Charlie"
```
```tree``` - Print a value to the jungle floor (console). Show off your findings!
```
tree "Look, a big tree!"
tree 10 + 5 # Prints 15
```
```ask()``` - Get input from the user. This is how you ask other apes important questions, like "Got any spare bananas?"
```
ape response = ask()
tree "You said: " + response
```
```if``` - Conditional execution. If a condition is true, do something. Otherwise, well, maybe do something else.
```
if bananas > 0 {
    tree "Eat banana!"
} else {
    tree "Must find more bananas."
}
```
```else``` - Alternative path for if statements. What to do when the if condition isn't met.

```swing``` - Loop a block of code. Keep swinging through the trees until your condition is met! The value after swing determines how many times the loop runs.
```
swing 3 {
    tree "Swinging!"
} # Prints "Swinging!" three times
```
```tribe``` - Define a function. Gather your instructions into a "tribe" that can be called upon later.
```
tribe greetApe(name) {
    tree "OOH-OOH-AAH-AAH, " + name + "!"
    give nil # Functions should always give something back, even if it's nothing (nil)
}
greetApe("Bob")
```
```give``` - Return a value from a function. What the tribe gives back after performing its task.
```
tribe getBananaCount() {
    give 7
}

ape count = getBananaCount()
tree "Total bananas: " + count
```

**Operators (The Primal Interactions)**
```ooh (+)``` - Addition and String Concatenation. For combining numbers or shouting phrases together.
```
tree 5 ooh 3 # Prints 8
tree "Big " ooh-ooh-aah-aah "Banana" # Prints "Big Banana"
```
```aah (-)``` - Subtraction. For when you've eaten some bananas.
```
tree 10 aah 4 # Prints 6
```

```eek (*)``` - Multiplication. For when you find a whole bunch of bananas.
```
tree 2 eek 5 # Prints 10
```
```ook (/)``` - Division. For sharing bananas (carefully!).
```
tree 10 ook 2 # Prints 5
```

```==``` - Equality. Are these two things exactly the same?
```
tree 5 == 5 # Prints true
tree "banana" == "apple" # Prints false
```

```!=``` - Inequality. Are these two things different?
```
tree 5 != 10 # Prints true
```
```<``` - Less than. Is the first value smaller than the second?
```
tree 3 < 5 # Prints true
```
```<=``` - Less than or equal to. Is the first value smaller than or equal to the second?
```
tree 5 <= 5 # Prints true
```
```>``` - Greater than. Is the first value larger than the second?
```
tree 8 > 2 # Prints true
```
```>=``` - Greater than or equal to. Is the first value larger than or equal to the second?
```
tree 7 >= 7 # Prints true
```

## Now, let's make some noise!

Compile your ```.ape``` file to ```.apb (Ape Bytecode)```:
```
./apeslang compile hellobanana.ape
```

If successful, you'll see: 
```
Compilation successful. 🦍🍌
```
Run your ```.apb``` bytecode:
```
./apeslang run hellobanana.apb

```

## Apelang in action.

1. The Classic "Hello, World!" 
```
tree "OOH-OOH-AAH-AAH! Hello, Jungle!"
```

Output:
```
🌴 🦍  OOH-OOH-AAH-AAH!  WELCOME TO THE BANANA JUNGLE  🦍 🌴ApesLang VM Output
OOH-OOH-AAH-AAH! Hello, Jungle!
```
2. Counting Bananas (Loops) bananas.ape
```
ape bananas = 5

swing bananas {
    tree "I have a banana!"
    bananas = bananas aah 1
}

```
Output:
```
🌴 🦍  OOH-OOH-AAH-AAH!  WELCOME TO THE BANANA JUNGLE  🦍 🌴ApesLang VM Output
I have a banana!
I have a banana!
I have a banana!
I have a banana!
I have a banana!
```

3. Conditional Foraging (If/Else) foraging.ape
```
ape weather = "rainy"

if weather == "sunny" {
    tree "Time to forage for berries!"
} else {
    tree "Stay in cave. Too wet for foraging."
}
```

Output:
```
🌴 🦍  OOH-OOH-AAH-AAH!  WELCOME TO THE BANANA JUNGLE  🦍 🌴ApesLang VM Output
Stay in cave. Too wet for foraging.
```
4. The Wise Old Ape (Functions/Tribes) wise_ape.ape
```
tribe wiseWords() {
    tree "Ape who chases two bananas catches none."
    tree "The early ape gets the worm... or the banana."
    give nil
}

wiseWords()
```

Output:
```
🌴 🦍  OOH-OOH-AAH-AAH!  WELCOME TO THE BANANA JUNGLE  🦍 🌴ApesLang VM Output
Ape who chases two bananas catches none.
The early ape gets the worm... or the banana.
```
5. Asking for More Bananas (Input) ask_banana.ape
```
tree "How many bananas do you have?"
ape myBananas = ask()
tree "You say you have " ooh myBananas ooh " bananas. OOH-OOH-AAH-AAH!"

```

Output (user input: 10):
```
🌴 🦍  OOH-OOH-AAH-AAH!  WELCOME TO THE BANANA JUNGLE  🦍 🌴ApesLang VM Output
How many bananas do you have?
10
You say you have 10 bananas. OOH-OOH-AAH-AAH!
```

## Apelang vs. Humanlang: A Tale of Two Eras

While Humanlang, with its modern Wi-Fi era, creates advanced networking capabilities and runs on fancy interpreters. Apelang is a much more less grounded language. 
Apelang is a compiled language, meaning your ape code is translated directly into raw, efficient bytecode that the VM executes. There's no middle-ape interpreter like THE TALKING SNAKE; it's a direct connection to the machine's primal core. 
This also means, unlike its futuristic descendant, Apelang doesn't concern itself with the like of networking or the internet.
Our apes communicate through shouts, gestures. If you want to connect to other apes, you'll have to do it the old-fashioned way: by swinging over to their tree.

## Known Bugs (or "Unpredictable Ape Behavior")
Sometimes the compiler throws a banana peel at you. Just try again.
The VM might occasionally decide to go on a spontaneous swinging spree. It's just embracing its inner ape.
Error messages are in "Ape-speak," which can be a bit cryptic. 

Good luck!
