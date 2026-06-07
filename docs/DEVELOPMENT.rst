Development environment and recovery
####################################

Persistence model
*****************

The devcontainer maps the host workspace into ``/workdir`` using a bind mount:

.. code-block:: json

   "workspaceMount": "source=${localWorkspaceFolder},target=/workdir,type=bind"

Therefore files under ``/workdir`` survive a container rebuild. Files installed
elsewhere inside the container do not survive and must be reproducible from the
Dockerfile or setup scripts.

The application repository is the source of truth. Zephyr, modules, and build
outputs are reproducible dependencies and must not contain customer source code.

Repository contents
*******************

* ``west.yml`` pins the Zephyr repository and imports its pinned dependencies.
* ``scripts/setup-workspace.sh`` initializes and updates a west workspace.
* ``scripts/build.sh`` performs a clean dual-core build.
* ``scripts/flash.sh`` flashes the generated dual-core build.
* ``.gitignore`` excludes generated build output and editor files.

First-time setup from a Git remote
**********************************

Create an empty directory, clone this repository, then initialize west:

.. code-block:: console

   mkdir zephyrproject
   cd zephyrproject
   git clone <YOUR_GIT_REMOTE_URL> digital_power_control
   ./digital_power_control/scripts/setup-workspace.sh
   ./digital_power_control/scripts/build.sh

The setup command downloads Zephyr and all modules at the revisions selected by
the pinned Zephyr commit. It can take significant time and disk space.

Recover after a devcontainer rebuild
************************************

Because ``/workdir`` is a bind mount, normally only reopen the rebuilt container
and run:

.. code-block:: console

   cd /workdir/zephyrproject
   ./digital_power_control/scripts/build.sh

If the entire host workspace was deleted, clone the application repository and
run the first-time setup procedure again.

Remote backup
*************

A local Git repository protects against accidental edits, but not host disk loss.
Add a company Git server or private GitHub/GitLab repository and push every
completed change:

.. code-block:: console

   cd digital_power_control
   git remote add origin <YOUR_GIT_REMOTE_URL>
   git push -u origin main

Do not commit ``zephyr/``, ``modules/``, or ``build-*`` into this repository.
