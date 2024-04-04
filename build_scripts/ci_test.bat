:: change the working directory to the exe's location
cd binaries

:: run the Spartan engine executable with the -ci_test flag
spartan_%1.exe -ci_test

:: wait for 15 seconds
timeout /t 15
