# What to do
I welcome anyone to contribute, and you can work on any aspect you prefer. If you need ideas on what to do:
- Check out the [issues](https://github.com/PanosK92/SpartanEngine/issues) section. I regularly open issues myself to keep track of them and also for others to tackle them.
- You can also [join our Discord server](https://discord.gg/TG5r2BS), a growing community of 250+ members, where you can ask for ideas on what to contribute.
- The readme page has a [roadmap section](https://github.com/PanosK92/SpartanEngine#roadmap), which is another way to see how you could help.
- Another great approach is to simply run the engine, see what doesn't work or could be improved.

# General
- Try to adhere to the [KISS](https://en.wikipedia.org/wiki/KISS_principle) principle whenever possible.
- Avoid [defensive programming](https://en.wikipedia.org/wiki/Defensive_programming). Instead, use asserts everywhere (offensive). If an assert pops up, it should be fixed ASAP.
- If **auto** is used, the type must be deducible at a glance. Long iterator types can also be replaced with auto.
- Choose the right tool for the job. Refrain from dogmas like "Only use orthodox C++" or "Only use modern C++".
- You can use tabs or spaces. The project will replace all tabs with 4 spaces to maintain a consistent look across IDEs.
- Use comments, but if you can name your code well enough that it doesn't need to be commented, that's ideal.
- If a class doesn't need to have multiple instances, make it static. Don't invite unnecessary complexity.

# Naming
Variables should be named using the snake_case naming convention.
```
bool name_like_this;
```
It's closer to natural language and easier for our brains to read.

# Alignment
When you are initialising a lot of variables, try to align them, in a symmetrical manner, like so:
```
m_matrix       = Matrix::Identity;
m_matrix_local = Matrix::Identity;
m_parent       = nullptr;
```
A Visual Studio extension that automatically aligns like this can be found [here](https://marketplace.visualstudio.com/items?itemName=cpmcgrath.Codealignment).

# If statements
If statements should always have brackets, the brackets should be on their own lines, like so:
```
if (condition)
{
    // code
}
```
The only exception is the early exit if statements, which can have no brackets, like so:
```
if (condition)
    return
```


# Const correctness
Try to use const for function parameters, if needed. But don't worry too much about const correctness.

# Workflow
Clone/fork, work, submit a pull request.
