
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
#include <iostream>


#include <opencv2/opencv.hpp>

#include "../headers/mainwindow.h"

//For test
#include "../headers/WebScraper.h"


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


    // Temporary test:
// Search VLR for T1 and add it to the existing local database.
    //bool added = WebScraper::scrapeMissingTeam("T1");

    // Print whether the missing-team scrape worked.
    //std::cout << "Missing-team scrape result: "
      //  << (added ? "SUCCESS" : "FAILED")
        //<< "\n";

    // Print the new total number of locally stored player records.
    //std::cout << "Final local player count: "
        //<< WebScraper::getPlayers().size()
      //  << "\n";
    //
    //return 0;

    //return 0;
    
    // Temporary full-database test:
    // Scrape all teams and players from the VLR ranking pages,
    // save them to players.txt, then reload the file into memory.
    //webScraper::scrapePlayers();

    // Print the total number of players now loaded from players.txt.
    //std::cout << "Final local player count: "
      //        << WebScraper::getPlayers().size()
        //      << "\n";

    //return 0;

    //Makes a Window object and show it to the user
    MainWindow mainWindow;
    mainWindow.show();

    // The MainWindow constructor now sets the application title.
    // mainWindow.setWindowTitle("Valorant-Vision");
    // Start with a larger window so the redesigned GUI has more space.
    mainWindow.resize(1100, 750);

    // Prevent the window from becoming too small for the GUI layout.
    mainWindow.setMinimumSize(900, 650);

    
    // Start the Qt event loop and keep the application running.
    return app.exec();

}
