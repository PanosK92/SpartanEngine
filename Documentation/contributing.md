

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
    m_matrix	    = Matrix::Identity;
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
Please ensure that your pull request is of decent quality. This means that you should always test what you write and also take as much time as you want to write what you want. In the end of the day, it's okay if there is a bug or two, I'll help you iron them out myself. However, due to time limitations and due to the definition of a pull request, I can't always devote an amount of time (fixing, reviewing, commenting) which is equal or higher that then amount of time it would have taken me to implement it myself. So to sum it up, I'll always try to be helpful but please try to do the same. Much appreciated.
