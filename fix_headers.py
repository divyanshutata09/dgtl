import os

header = '''/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

'''

for root, _, files in os.walk('include'):
    for f in files:
        if f.endswith('.hpp') or f.endswith('.h'):
            path = os.path.join(root, f)
            with open(path, 'r', encoding='utf-8') as file:
                content = file.read()
            if 'TataG Systems. All rights reserved.' not in content:
                with open(path, 'w', encoding='utf-8') as file:
                    file.write(header + content)
