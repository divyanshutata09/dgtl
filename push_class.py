import os

source_file = r'C:\Users\bltat\Documents\1-5-26-TataG\TataG\DGTL\include\dgtl\api\Frontend_Pipeline.hpp'
target_file = r'include\dgtl\core\FrontendHelpers.hpp'

with open(source_file, 'r', encoding='utf-8') as f:
    source_lines = f.readlines()

in_class = False
class_lines = []

for line in source_lines:
    if line.startswith('class FrontendPipelineLowerer'):
        in_class = True
    if in_class:
        class_lines.append(line)
        if line.startswith('};'):
            break

with open(target_file, 'r', encoding='utf-8') as f:
    target_content = f.read()

# Insert the class right before the closing brace of namespace dgtl::core
target_content = target_content.replace('} // namespace dgtl::core', ''.join(class_lines) + '\n} // namespace dgtl::core')

with open(target_file, 'w', encoding='utf-8') as f:
    f.write(target_content)
