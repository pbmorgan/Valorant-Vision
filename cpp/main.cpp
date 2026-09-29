
/*
Includes:
QT: Tool for Making GUIs in C++
OpenCV: Libary for image processing, ml, and real-time Comptuer Vision
mainwindow.h : H file for making the window

Description: 
*/

#include <QtWidgets>
#include <QVBoxLayout>
#include <QApplication>
#include <QLabel>
#include <QImage>
#include <QPushButton>
#include <QLayout>

#include <opencv2/opencv.hpp>

#include "../headers/mainwindow.h"


int main(int argc, char* argv[])
{
	//Makes a Application object that is needed for any QT application,
    //It takes the command line arguments
    QApplication app(argc, argv);
       
	//Created a Qfile object that will load the style.qss
    //File from the resources folder
    QFile stylesheet(":/style/style.qss");

	//Opens the stylesheet file in read-only and text mode, and if successful,
    //Sets the application's style sheet to the contents of the file. Then it closes the file.
    if (stylesheet.open(QIODevice::ReadOnly | QIODevice::Text)){
		//Sets the application's style sheet to the contents of the file
        qApp->setStyleSheet(stylesheet.readAll());
        stylesheet.close();
    }

    //Makes a Window object and show it to the user
    MainWindow mainWindow;
    mainWindow.show();

    //mainWindow.setWindowTitle("Valorant-Vision");
    mainWindow.resize(900, 600); // initial window size for better viewing
    mainWindow.setMinimumSize(700, 400); // set a minimum size so, it keeps the UI clean

    

    return app.exec();
}
