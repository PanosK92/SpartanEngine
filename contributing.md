# Why Contribute

The goal is to see you evolve beyond the version of yourself who initially started contributing here.
By engaging with this project, you gain knowledge, forge connections, and potentially secure job opportunities (via Discord).
This is a space where you're free to explore, experiment, and excel. Make the most of it and see how far you can go.

# Getting Started

- **Start small.** If it's your first time, pick a small task and go through the full cycle: clone, work, submit. This gives you a feel for the workflow, the scale of the project, and maybe even how to have fun while doing it.
- **Mind the scope.** The bigger the task, the higher the chance you'll drop it due to complexity, scale, or boredom. If you feel stressed, remember that there are zero expectations.
- **Have fun first.** Prioritize practical action over extensive theorizing. Spending four hours discussing a task that takes 30 minutes to complete is inefficient.

# What to Work On

- Browse the [issues](https://github.com/PanosK92/SpartanEngine/issues) page. Issues are regularly opened for tracking and for others to pick up.
- [Join our Discord server](https://discord.gg/TG5r2BS), a growing community of 600+ members, where you can ask for ideas on what to contribute.
- Run the engine yourself, see what doesn't work or what could be improved, and take it from there.

# Workflow

All contributions go through your own fork, not the main SpartanEngine repo.

1. Fork the SpartanEngine repo to your GitHub account.
2. Clone your fork locally.
3. Create a branch and do your work there.
4. Push the branch to your fork.
5. Open a Pull Request from your fork/branch into SpartanEngine.

# Pull Request Guidelines

- **Functional code.** Your PR can be incomplete, but the code it introduces must be operational. Non-functional PRs block progress.
- **Incremental changes.** It's perfectly fine for your PR to be part of a larger task. Small, functional steps are encouraged.
- **Stay focused.** Keep each PR scoped to one issue or change. Avoid touching unrelated parts of the engine, it makes review harder and increases merge conflicts.
- **Communicate.** If you're looking for collaboration, mention it in the PR description or on Discord.
- **Be self-sufficient.** This is an open source project, not a classroom. Maintainers provide architectural direction, not line-by-line coding instruction.

# Coding Style

## General

- Follow the [KISS](https://en.wikipedia.org/wiki/KISS_principle) principle whenever possible. A term coined by the lead engineer of the SR-71 Blackbird.
- Avoid [defensive programming](https://en.wikipedia.org/wiki/Defensive_programming). Use asserts everywhere (offensive). If an assert fires, fix it immediately.
- **auto** should only be used when the type is obvious at a glance. Long iterator types are a reasonable exception.
- Choose the right tool for the job. Avoid dogmas like "only orthodox C++" or "only modern C++".
- Tabs or spaces are both fine. The project automatically replaces tabs with 4 spaces for consistency across IDEs.
- Use comments when needed, but if you can name things well enough to not need them, that's ideal.
- If a class doesn't need multiple instances, make it static. Don't invite unnecessary complexity.
- With a project of this scale, simplicity is your best friend.

<img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/simplicity.jpg"/>

## Naming

Variables use the snake_case convention.
```
bool name_like_this;
```
It's closer to natural language and easier for our brains to read.

## Alignment

When initializing multiple variables, align them symmetrically.
```
m_matrix       = Matrix::Identity;
m_matrix_local = Matrix::Identity;
m_parent       = nullptr;
```
A Visual Studio extension for automatic alignment can be found [here](https://marketplace.visualstudio.com/items?itemName=cpmcgrath.Codealignment).

## Braces

Braces go on their own lines.
```
if (condition)
{
    // code
}
```
The only exception is early-exit statements, which can omit braces.
```
if (condition)
    return;
```

## Const Correctness

Use const for function parameters when appropriate, but don't obsess over it.

# The Bigger Picture

Everything above will make you a solid contributor. But if you want to do exceptional work, the kind you're proud of, there's a bigger vision waiting.

We're building something inspired by [this video](https://youtu.be/R3QvniaZ5qM?t=53) that captured millions: a cinematic night city running in real-time on Spartan. Watch it. Then look at the engine. The delta between them is your roadmap. You don't need anyone to assign you tasks. Your taste tells you what's not good enough. Your skills tell you what you can fix.

**[Read the full plan](https://github.com/PanosK92/SpartanEngine/blob/master/plan.md)** - and see how far we can go together.
