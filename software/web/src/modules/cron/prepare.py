import os
import sys
import importlib.util
import importlib.machinery

software_dir = os.path.realpath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..'))

def create_software_module():
    software_spec = importlib.util.spec_from_file_location('software', os.path.join(software_dir, '__init__.py'))
    software_module = importlib.util.module_from_spec(software_spec)

    software_spec.loader.exec_module(software_module)

    sys.modules['software'] = software_module

if 'software' not in sys.modules:
    create_software_module()

from software import util

imports = []
sideeffects_imports = []
already_imported = set({})
trigger = []
action = []

for plugin in util.find_frontend_plugins('Cron', 'Trigger'):
    import_path = os.path.join('..', plugin.module_name, plugin.import_name)

    if import_path not in already_imported:
        imports.append("import * as {0}_trigger from '{1}'".format(plugin.module_name, import_path))
        sideeffects_imports.append("import '{0}'".format(import_path))
        already_imported.add(plugin.module_name)

    for interface_name in plugin.interface_names:
        trigger.append('{0}_trigger.{1}'.format(plugin.module_name, interface_name))

for plugin in util.find_frontend_plugins('Cron', 'Action'):
    import_path = os.path.join('..', plugin.module_name, plugin.import_name)

    if import_path not in already_imported:
        imports.append("import * as {0}_action from '{1}'".format(plugin.module_name, import_path))
        sideeffects_imports.append("import '{0}'".format(import_path))
        already_imported.add(plugin.module_name)

    for interface_name in plugin.interface_names:
        action.append('{0}_action.{1}'.format(plugin.module_name, interface_name))

util.specialize_template("api.ts.template", "api.ts", {
    "{{{imports}}}": '\n'.join(imports),
    "{{{trigger}}}": '\n    | '.join(trigger),
    "{{{action}}}": '\n    | '.join(action),
    })

util.specialize_template("sideeffects.tsx.template", "sideeffects.tsx", {
    "{{{imports}}}": '\n'.join(sideeffects_imports),
})
