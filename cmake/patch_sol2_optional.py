import sys

path = sys.argv[1]
with open(path) as f:
    content = f.read()

# Fix 1: optional_base<T> emplace -- this->construct() doesn't set this->m_value correctly
content = content.replace(
    '*this = nullopt;\n\t\t\tthis->construct(std::forward<Args>(args)...);\n\t\t\treturn value();',
    '*this = nullopt;\n\t\t\tthis->m_value = std::addressof((std::forward<Args>(args), ...));\n\t\t\treturn value();')

# Fix 2: optional<T&> emplace -- this->construct() doesn't exist in ref specialization
content = content.replace(
    '*this = nullopt;\n\t\t\tthis->construct(std::forward<Args>(args)...);\n\t\t}',
    '*this = nullopt;\n\t\t\tm_value = std::addressof((std::forward<Args>(args), ...));\n\t\t\treturn *m_value;\n\t\t}')

with open(path, 'w') as f:
    f.write(content)

remaining = content.count('this->construct(std::forward<Args>(args)')
if remaining > 0:
    print(f'WARNING: {remaining} unpatched occurrences remain')
    sys.exit(1)
print('sol2 optional_implementation.hpp patched')
