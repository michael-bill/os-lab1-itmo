import random

def generate_numbers_file(filename, max_number, target_substring):
    numbers = [str(i) for i in range(1, max_number + 1)]
    insert_position = random.randint(0, len(numbers))
    numbers.insert(insert_position, target_substring)
    content = ' '.join(numbers)
    with open(filename, 'w') as file:
        file.write(content)

filename = 'large_text_file.txt'
max_number = 1_000_000
target_substring = 'target_substring'
generate_numbers_file(filename, max_number, target_substring)
