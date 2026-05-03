### Program optimization

1. Start by mapping the assembly instructions to the source code. All the reasoning should be performed with source code first. The assembly can only be used as a supplementary source.
2. Analyze the available data, looking where the majority of the run time is spent. Always look at the code as a whole. Do not stop after finding a bunch of interesting spots.
3. Figure out what algorithms are in use, how the data is structured and how it flows, reason about trade-offs taken.
4. Reason if the code can be made to perform better. Note that some code will already be optimal, despite having hot spots.
5. Formulate the optimization strategies and present them to the user.
6. Do not provide concrete speed up percentages. It is only possible to know how faster the code is by measuring it after the changes. You can't do that.
