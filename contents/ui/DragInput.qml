import QtQml
import "native-input" as NativeInput

NativeInput.MoveInput {
    readonly property string translationDomain: registerTranslationCatalog(Qt.resolvedUrl("../locale"))
}
