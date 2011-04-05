# -*- coding: utf-8 -*-

import sys, io, json
from PyQt4 import QtCore, QtGui
from PyQt4.QtCore import QString
from PyQt4.QtGui import QColor
from TestResultViewerUI import Ui_MainWindow

class MainWindow(QtGui.QMainWindow, Ui_MainWindow):
  def __init__(self):
    QtGui.QMainWindow.__init__(self)
    self.setupUi(self)

  @QtCore.pyqtSignature("")
  def on_actionOpen_triggered(self):
    filename = QtGui.QFileDialog.getOpenFileName(self, 'Open JSON Test Results')
    if filename != None:
      with io.open(unicode(filename)) as f:
        self.test_results = json.load(f)
        self.onLoadData()

  def on_TestResultList_itemSelectionChanged(self):
    selected_item = self.TestResultList.selectedItems()[0]
    stage = getattr(selected_item, 'stage', None)
    if stage != None:
      self.Command.setText('Command: ' + stage.get('command', ''))
      self.ExitCode.setText('Exit: ' + unicode(stage.get('exit', '')))
      self.ReferenceExitCode.setText('Reference Exit: ' + unicode(stage.get('reference_exit', '')))
      self.Stdout.setText(stage.get('stdout', ''))
      self.Stderr.setText(stage.get('stderr', ''))
      self.ReferenceStdoutStderr.setText(stage.get('reference_stdout', ''))

  def onLoadData(self):
    self.TotalTests.setText('Total Tests: ' + unicode(len(self.test_results)))
    numfailed = 0
    for tr in self.test_results:
      total_time = 0.0
      stages = []

      for stage in tr['stages']:
        child = QtGui.QTreeWidgetItem([stage['name'], unicode(stage['elapsed-time'])])
        if len(stage.get('stderr', '')) > 0:
          child.setBackgroundColor(0, QColor('Yellow'))
        child.stage = stage
        total_time += stage['elapsed-time']
        stages.append(child)

      item = QtGui.QTreeWidgetItem([tr['name'], unicode(total_time)])
      item.test = tr
      if tr['failed']:
        numfailed += 1
        item.setBackgroundColor(0, QColor('Red'))
        # Also set the last stage to failed.
        stages[-1].setBackgroundColor(0, QColor('Red'))
      item.addChildren(stages)
      self.TestResultList.addTopLevelItem(item)
    self.PercentFailed.setText('% Failed: ' + unicode((float(numfailed) / len(self.test_results)) * 100))

if __name__ == "__main__":
  app = QtGui.QApplication(sys.argv)
  w = MainWindow()
  w.show()
  sys.exit(app.exec_())
