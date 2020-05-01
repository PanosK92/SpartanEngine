

This project is open to contributors, so if you feel like you can help with anything, don't hesitate.

I will probaly update and adds things to this page, but for now, here are few simple guidlines that can help give you an idea of how to approach things.

# Code
- Try to follow the [KISS](https://en.wikipedia.org/wiki/KISS_principle) principle whenever possible.

- Choose the right tool for the job. Abstain from dogmas like "Only use orthodox c++" or "Only use modern C++".

- If you think you should add a comment, first try to see if the code structure and naming conventions can be more self documenting. If not, a comment can be added. In general, comments are good, as long as they explain what's inherently not obvious.

- Member variables should start with the **m_** prefix

- Use real tabs that equal 4 spaces (which is the project's default), especially when initializing a lot of variables.
    ```
    m_positionLocal	= Vector3::Zero;
    m_rotationLocal	= Quaternion(0, 0, 0, 1);
    m_scaleLocal	= Vector3::One;
    m_matrix        = Matrix::Identity;
    m_matrixLocal	= Matrix::Identity;
    m_wvp_previous	= Matrix::Identity;
    m_parent        = nullptr;
    ```
    
- Always use braces on code blocks, even with the encapsulated code is one line.
	```
	if (x)
    {
	    function();
	}
	```
- The auto keyword. Some people hate it, others love it, but as with most things in life, the truth lies in balance.
	```
    /*Good*/    auto position   = Vector3(0, 0, 0);   // The type is deducable by eye, because of the constructor.
    /*Bad*/     auto data       = GetAllData();       // The type is not deducable by eye, the definition of the functions has to be looked for.
	```
- Rules are rarely perfect, if you stumble upon some special case, let me know and we'll discuss about what might be best.

# Pull Requests
Please ensure that your pull request is of decent quality, by that I mean that it's ok if it contains a bug or two (I'll help you fixed them) but it's not ok if it's half-done or brakes/crashes the engine. I want all the help I can get but at the same time I have to ensure that quality of the engine is decent, I hope you understand. Thanks.
