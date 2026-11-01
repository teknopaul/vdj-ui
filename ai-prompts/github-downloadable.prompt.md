# Downloadable .deb for Ubuntu

Github has `Actions` to build code and create downloadable artifacts. I think actions are free for open source projects like this.

We have a couple of problems to resolve.

- We only support Ubuntu 24.04 LTS, thats OK for now. But in the future it might be good to support other distros, we should label the export somehow as Ubuntu 24.04
- We need to package libcdj binaries, so we need to add them to the git repo to be able to include them in the github package
- We may not have specified all the dependencies in the .deb because we have presumed my PC up to now.
