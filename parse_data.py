import os
import pandas as pd

# Define the directory containing the files
directory = 'result_better_categories'

# Initialize an empty dictionary to store data for the DataFrame
data_dict = {}

# Loop through each file in the directory
for filename in os.listdir(directory):
    if filename.endswith(".txt"):  # Check if the file is a text file
        file_path = os.path.join(directory, filename)
        
        # Read the file
        with open(file_path, 'r') as file:
            # Initialize a dictionary to store data for the current file
            file_data = {}
            
            # Process each line in the file
            for line in file:
                if line.startswith("inst tuple:"):
                    # Extract the tuple and the percentage
                    parts = line.split(", cumulative CCs:")
                    tuple_part = parts[0].replace("inst tuple: <", "").replace(">", "").strip()
                    percentage_part = float(parts[1].strip().replace("%", ""))
                    
                    # Store the data
                    file_data[tuple_part] = percentage_part
        
        # Add the data to the main dictionary with the filename as the key
        data_dict[filename] = file_data

# Create a DataFrame from the dictionary
df = pd.DataFrame(data_dict)

df = df.fillna(0)

# Transpose the DataFrame to have filenames as columns if desired
df = df.transpose()

df.to_csv('data.csv', index=True)


# Display the DataFrame
print(df)
