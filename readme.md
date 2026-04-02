# QMeshLab A minimal qt6.11 application with CMakeLists.txt 
that uses qrhi to render a set of triangles. The application is a single document interface (SDI) application that creates a main window and a central widget where the rendering takes place. The rendering is done using the QRhi API, which provides a high-level abstraction over the underlying graphics API (such as Vulkan, Direct3D, or OpenGL). The application initializes the QRhi context, creates a vertex buffer for the triangle vertices, and sets up a simple shader to render the triangles. The main loop of the application handles events and triggers the rendering of the triangles on each frame. The application is designed to be minimal and serves as a starting point for developers who want to learn how to use QRhi for rendering in a Qt application. It demonstrates the basic setup and rendering process.
There is a menu bar with a "File" menu that contains an "Exit" action to close the application. The application also includes error handling for the QRhi initialization and rendering process, ensuring that any issues are logged appropriately. The code is organized into a main application class that inherits from QMainWindow, and a separate rendering widget class that handles the QRhi rendering logic. This structure allows for a clean separation of concerns and makes it easier to extend the application in the future with additional features or more complex rendering techniques.
To build and run the application, you will need to have Qt 6.11 installed on your system, along with CMake for building the project. You can use the following commands in your terminal to build the application:

```bashmkdir build
cd build
cmake ..
cmake --build .
./QMeshLab
``` 