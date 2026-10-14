// tttt's overlay (test/oracle, plan 56): mapped into tsgo's checkout as
// internal/testutil/contentmappertest/tttt_stdio.go, so cmd/tttt-test-mapper reaches the package's unexported
// registry. TtttServeStdio is the in-process spawner's Spawn (spawner.go) with stdin/stdout as the connection.
package contentmappertest

import (
	"context"
	"errors"
	"io"
	"os"

	"github.com/microsoft/TypeScript/tsc/internal/ipc"
)

type ttttStdio struct {
	io.Reader
	io.Writer
}

func (ttttStdio) Close() error {
	return errors.Join(os.Stdin.Close(), os.Stdout.Close())
}

func TtttServeStdio(command []string) error {
	handler, err := handlerForMapper(command, nil)
	if err != nil {
		return err
	}
	if command[0] != DynamicVerbatimMapper {
		handler = staticProjectHandler{Handler: handler}
	}
	err = ipc.NewAsyncConn(ttttStdio{Reader: os.Stdin, Writer: os.Stdout}, handler).Run(context.Background())
	if errors.Is(err, io.EOF) {
		return nil
	}
	return err
}
