# Why do it
The goal is to see you evolve beyond the version of yourself who initially started contributing here.
By engaging with this project, you can gain knowledge, forge connections, or even secure job opportunities (Discord).
This platform offers you the freedom to explore, experiment, and excel. Make the most of it and see how far you can go.

# How to do it
- Starting small: If it's your first time contributing, pick a small task and go through the process of cloning, working, and submitting. This will give you an idea of the workflow, the scale of the project, and maybe even how to have fun while doing it.
- Task complexity: The bigger the task you choose, the higher the probability that you'll end up dropping it due to complexity, scale, or boredom. You might even feel stressed, which is the wrong way to go about it. If you are in this place, remember that I have zero expectations.
- Your priorities: Your first priority should be to have fun. Secondly, prioritize practical action over extensive theorizing. For instance, spending four hours discussing a task that could be completed in 30 minutes is inefficient.
- Workflow overview: The workflow is: clone/fork, work, submit a pull request.

# Pull Request (PR) Guidelines
- Functionality: Ensure your PR is functional. While it can be incomplete, the code it introduces should be operational.
- Collaboration restrictions: Other contributors generally can't modify your PR directly. Hence, non-functional PRs can obstruct the project's progress.
- Iterative improvements: It's okay for your PR to be a part of a larger task. Incremental, functional changes are encouraged.
- Communication: If you're looking for collaboration on your PR, communicate this in your PR description or through our Discord community.

# What to do
- Check out the [issues](https://github.com/PanosK92/SpartanEngine/issues) section. I regularly open issues myself to keep track of them and also for others to tackle them.
- You can also [join our Discord server](https://discord.gg/TG5r2BS), a growing community of 300+ members, where you can ask for ideas on what to contribute.
- Another great approach is to simply run the engine, see what doesn't work or could be improved.

# Coding style

## General advice
- Try to adhere to the [KISS](https://en.wikipedia.org/wiki/KISS_principle) principle whenever possible.
- Avoid [defensive programming](https://en.wikipedia.org/wiki/Defensive_programming). Instead, use asserts everywhere (offensive). If an assert pops up, it should be fixed ASAP.
- If **auto** is used, the type must be deducible at a glance. Long iterator types can also be replaced with auto.
- Choose the right tool for the job. Refrain from dogmas like "Only use orthodox C++" or "Only use modern C++".
- You can use tabs or spaces. The project will replace all tabs with 4 spaces to maintain a consistent look across IDEs.
- Use comments, but if you can name your code well enough that it doesn't need comments, that's ideal.
- If a class doesn't need multiple instances, make it static. Don't invite unnecessary complexity.

## Naming
Variables should be named using the snake_case naming convention.
```
bool name_like_this;
```
It's closer to natural language and easier for our brains to read.

## Alignment
When you are initializing a lot of variables, try to align them in a symmetrical manner, like so:
```
m_matrix       = Matrix::Identity;
m_matrix_local = Matrix::Identity;
m_parent       = nullptr;
```
A Visual Studio extension that automatically aligns like this can be found [here](https://marketplace.visualstudio.com/items?itemName=cpmcgrath.Codealignment).

## If statements
When you are initializing a lot of variables, try to align them in a symmetrical manner, like so:
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
## Const correctness
Try to use const for function parameters, if needed. But don't worry too much about const correctness.
