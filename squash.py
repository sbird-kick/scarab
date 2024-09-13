import pandas as pd

# Read the CSV file into a DataFrame
df = pd.read_csv('data.csv', index_col=0)

# Remove the '.txt' from the index (filenames)
df.index = df.index.str.replace('.txt', '', regex=False)

# Extract the meaningful prefix (the part before the last underscore '_')
df.index = df.index.str.rsplit('_', n=1).str[0]

# Group by the new base names and calculate the average for each group
df_grouped = df.groupby(df.index).mean()

# Save the merged and averaged DataFrame to a new CSV file
df_grouped.to_csv('squashed.csv', index=True)
