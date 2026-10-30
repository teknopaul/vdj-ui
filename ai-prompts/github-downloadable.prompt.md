# Downloadable .deb for Ubuntu

Github has `Actions` to build code and create downloadable artifacts. I think actions are free for open source projects like this.

We have a couple of problem sto resolve.

- We only support Ubuntu 24.04 LTS, thats OK for now. But int he future it might be good to support otehr distros
- We need to package libcdj binaries so we need to add them to the git repo to be abel to include them in the github package
- We probably have not roperly specified dependencies in teh .deb becuse we have presumed my PC upto now.
